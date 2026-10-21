"""Validate reference export without third-party Python modules.
Usage: python tools/synth/check_reference.py build/synth-foundation/reference.wav
"""
import array
import json
from pathlib import Path
import sys
import wave

path = Path(sys.argv[1])
with wave.open(str(path), "rb") as wav:
    assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 44100)
    pcm = array.array("h", wav.readframes(wav.getnframes()))
if sys.byteorder != "little":
    pcm.byteswap()
rate, segment = 44100, 48510
assert len(pcm) == 5 * segment
report = {"renderer": "VariableRateVoiceBlockRenderer", "sample_rate": rate,
          "duration_seconds": len(pcm)/rate, "peak_pcm16": max(map(abs, pcm)), "notes": []}
for index, note in enumerate((24, 60, 69, 127)):
    samples = pcm[index*segment:(index+1)*segment]
    # Interpolated positive crossings in the high-level body avoid quantized tails.
    crossings = []
    for i in range(1000, 30000):
        if samples[i-1] <= 0 < samples[i]:
            crossings.append(i-1 - samples[i-1]/(samples[i]-samples[i-1]))
    frequency = (len(crossings)-1)*rate/(crossings[-1]-crossings[0])
    expected = 440 * 2**((note-69)/12)
    assert abs(frequency/expected-1) < 0.001
    assert samples[0] == 0 and not any(samples[rate-1:])
    assert any(samples[rate-1500:rate-500])
    assert max(map(abs, samples)) <= 2048
    report["notes"].append({"note": note, "expected_hz": expected, "measured_hz": frequency,
                            "peak_pcm16": max(map(abs, samples)), "envelope_seconds": 1.0,
                            "zero_at_and_after_frame": rate-1})
sequence = pcm[4*segment:]
assert sequence[11025] == sequence[11024]
assert sequence[11100] == sequence[11099]
release_frames = (rate*15+500)//1000
assert not any(sequence[22050+release_frames-1:])
report["sequence"] = {"retrigger_frames": [11025, 11100], "off_frame": 22050,
                      "release_frames": release_frames, "release_ms": release_frames*1000/rate,
                      "continuity_and_silent_tail": "PASS"}
report["checks"] = "PASS: tuning, peak/headroom, one-second zero endpoints, retrigger continuity, OFF tail"
output = path.with_suffix(".json")
output.write_text(json.dumps(report, indent=2)+"\n")
print(json.dumps(report, indent=2))
