#!/bin/bash
# Diagnostic capture and read-only system evidence around the normal launcher.
launcher_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
normal="$launcher_dir/BroTracker Terminal.sh"
base="$HOME/BroTracker/diagnostics"
umask 077
if ! mkdir -p -- "$base"; then
    echo "Diagnostics unavailable: cannot create $base" >&2
    exec bash "$normal"
fi
stamp=$(date +%Y%m%d-%H%M%S 2>/dev/null) || stamp=undated
run=$(mktemp -d "$base/$stamp.XXXXXX") || exec bash "$normal"
export BROTRACKER_DIAGNOSTICS_DIR="$run"
printf 'wall_time\tmonotonic_time\tsource\tbridge_instance\tevent\tdetails\n' > "$run/events.tsv"
btx_diag_event() {
    local kind=$1 details=$2 instance=${3:--} source=${4:-collector-observation} wall mono unused
    details=${details//\\/\\\\}
    details=${details//$'\t'/\\t}
    details=${details//$'\n'/\\n}
    details=${details//$'\r'/\\r}
    wall=$(date +%Y-%m-%dT%H:%M:%S%z)
    read -r mono unused < /proc/uptime
    if command -v flock >/dev/null; then
        ( flock -x 9; printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$wall" "$mono" "$source" "$instance" "$kind" "$details" >&9 ) 9>> "$BROTRACKER_DIAGNOSTICS_DIR/events.tsv"
    else echo 'Timeline unavailable: missing flock'; fi
}
export -f btx_diag_event
exec >> "$run/collector.log" 2>&1
printf 'Diagnostic run: %s\n' "$run"
btx_diag_event run_start "directory=$run" - diagnostics-source
log=/tmp/brotracker-arkos.log
owner=$BASHPID
normal_pid=
worker_pid=
flush() {
    # Flush data during the run, not just on orderly shutdown.
    command -v sync >/dev/null || { echo 'Missing sync: power-loss persistence not guaranteed'; return; }
    sync -f "$run" 2>/dev/null || sync
}
read_file() {
    printf '%s: ' "$1"
    cat -- "$1" 2>&1 || echo '[unreadable]'
}
command_report() {
    if command -v "$1" >/dev/null 2>&1; then
        "$@" 2>&1 || printf '[failed: %s]\n' "$*"
    else
        printf '[missing tool: %s]\n' "$1"
    fi
}
inventory() {
    for dev in /sys/bus/usb/devices/*; do
        [[ -r "$dev/idVendor" ]] || continue
        printf '\nUSB %s\n' "$dev"
        for field in idVendor idProduct product serial speed busnum devnum; do read_file "$dev/$field"; done
        for interface in "$dev":*; do
            [[ -e "$interface" ]] || continue
            printf 'interface %s driver: ' "$interface"
            readlink -f "$interface/driver" 2>&1 || echo '[unbound/unreadable]'
            for field in bInterfaceNumber bAlternateSetting bInterfaceClass bInterfaceSubClass; do
                read_file "$interface/$field"
            done
            for endpoint in "$interface"/ep_*; do
                [[ -d "$endpoint" ]] || continue
                for field in bEndpointAddress bmAttributes wMaxPacketSize bInterval interval type; do
                    [[ ! -e "$endpoint/$field" ]] || read_file "$endpoint/$field"
                done
            done
        done
    done
    for node in /sys/class/tty/ttyACM* /sys/class/sound/card*; do
        [[ -e "$node" ]] || continue
        printf '%s device: ' "$node"
        readlink -f "$node/device" 2>&1 || echo '[unreadable]'
    done
    ls -l /dev/ttyACM* /dev/snd/* 2>&1
    read_file /proc/asound/cards
}
snapshot() {
    printf '\n=== snapshot %s ===\n' "$(date -Iseconds 2>/dev/null)"
    command_report uptime
    inventory > "$run/inventory.next" 2>&1
    if ! cmp -s "$run/inventory.next" "$run/inventory.last"; then
        echo 'USB/CDC/ALSA inventory changed (or first snapshot)'
        cat "$run/inventory.next"
        cp -- "$run/inventory.next" "$run/inventory.last"
    fi
    # Driver-reported USB packet interval/current frequency and live PCM
    # state complement negotiated userspace settings; never write proc/sysfs.
    for file in /proc/asound/card*/stream* /proc/asound/card*/pcm*/sub*/hw_params /proc/asound/card*/pcm*/sub*/status; do
        [[ -e "$file" ]] || continue
        read_file "$file"
    done
    command_report arecord -l
    command_report aplay -l
    for card in /sys/class/sound/card[0-9]*; do
        [[ -e "$card" ]] || continue
        index=${card##*/card}
        command_report amixer -c "$index" contents
    done
    # Full boot ring includes events before EmulationStation/this entry starts.
    command_report dmesg > "$run/kernel.next" 2>&1
    if ! cmp -s "$run/kernel.next" "$run/kernel.last"; then
        kernel_sequence=$((kernel_sequence + 1))
        cp -- "$run/kernel.next" "$run/kernel-$kernel_sequence.log"
        cp -- "$run/kernel.next" "$run/kernel.last"
        printf 'Kernel ring changed: kernel-%s.log\n' "$kernel_sequence"
    fi
}
copy_log() {
    [[ -f "$log" ]] || return
    size=$(wc -c < "$log")
    if ((size < offset)); then
        printf '\n[normal log truncated; restarting capture]\n' >> "$run/btx-stream.log"
        offset=0
    fi
    # Append bytes promptly; the normal launcher remains the sole /tmp writer.
    if ((size > offset)); then
        head -c "$size" "$log" | tail -c "+$((offset + 1))" >> "$run/btx-stream.log"
        while IFS= read -r line; do
            kind=log_observation
            case "$line" in
                *'audio '*attempt*) kind=bridge_recovery_attempt ;;
                *'USB bring-up:'*'TX:'*) kind=command_transmitted ;;
                *'USB bring-up:'*'RX:'*) kind=response_observed ;;
                *'requested'*) kind=control_request ;;
                *'disconnect'*|*'Disconnected'*) kind=disconnect_observed ;;
                *'connected'*|*'handshake'*) kind=connection_observed ;;
                *'Discontinuity:'*) kind=alsa_xrun_observed ;;
            esac
            btx_diag_event "$kind" "$line"
        done < <(head -c "$size" "$log" | tail -c "+$((offset + 1))")
        offset=$size
    fi
}
finish() {
    result=$1
    trap - EXIT
    trap '' INT TERM HUP
    if [[ -n "$normal_pid" ]]; then
        kill -TERM "$normal_pid" 2>/dev/null || true
        wait "$normal_pid" 2>/dev/null || true
    fi
    if [[ -n "$worker_pid" ]]; then
        kill -TERM "$worker_pid" 2>/dev/null || true
        wait "$worker_pid" 2>/dev/null || true
    fi
    [[ ! -f "$log" ]] || cp -- "$log" "$run/btx-final.log"
    printf 'Finalized %s; launcher status=%s\n' "$(date -Iseconds 2>/dev/null)" "$result"
    printf '%s\n' "$result" > "$run/exit-status"
    btx_diag_event run_end "launcher_status=$result" - diagnostics-source
    # Producers are reaped; order source timestamps for the finalized manifest.
    { head -n 1 "$run/events.tsv"; tail -n +2 "$run/events.tsv" | sort -s -t $'\t' -k2,2n; } > "$run/events.sorted"
    mv -- "$run/events.sorted" "$run/events.tsv"
    flush
    exit "$result"
}
trap 'finish "$?"' EXIT
trap 'finish 143' TERM
trap 'finish 130' INT
# Ignore network/session hangup; Ports operation does not require SSH.
trap '' HUP
command_report date -Iseconds
command_report uptime
command_report uname -a
command_report id
for file in "$normal" "$launcher_dir/brotracker/BroTrackerTerminal" "$launcher_dir/brotracker/BroTrackerAlsaBridge"; do
    command_report sha256sum "$file"
done
kernel_sequence=0
snapshot >> "$run/snapshots.log" 2>&1
flush
bash "$normal" & normal_pid=$!
(
    delay_pid=
    stop_worker() {
        trap '' TERM INT
        [[ -z "$delay_pid" ]] || { kill -TERM "$delay_pid" 2>/dev/null; wait "$delay_pid" 2>/dev/null; }
        copy_log
        flush
        exit 0
    }
    trap - EXIT
    trap stop_worker TERM INT
    offset=0
    tick=0
    while kill -0 "$normal_pid" 2>/dev/null; do
        parent_stat=
        if ! read -r parent_stat < "/proc/$owner/stat" 2>/dev/null || [[ "${parent_stat##*) }" == Z* ]]; then
            echo 'Diagnostic owner disappeared; stopping normal launcher'
            kill -TERM "$normal_pid" 2>/dev/null || true
            stop_worker
        fi
        copy_log
        if ((tick % 5 == 0)); then snapshot >> "$run/snapshots.log" 2>&1; fi
        flush
        tick=$((tick + 1))
        sleep 1 & delay_pid=$!
        wait "$delay_pid" || true
        delay_pid=
    done
    stop_worker
) & worker_pid=$!
result=0
wait "$normal_pid" || result=$?
normal_pid=
finish "$result"
