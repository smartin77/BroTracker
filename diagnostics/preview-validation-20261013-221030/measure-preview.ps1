param([string]$EvidenceDirectory)
$ErrorActionPreference='Stop'
if(Get-Process BroTrackerTerminal -ErrorAction SilentlyContinue) { throw 'BTX owns serial; refusing second owner' }
$port=[System.IO.Ports.SerialPort]::new('COM3',115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$port.DtrEnable=$true; $port.RtsEnable=$true; $port.ReadTimeout=10; $port.WriteTimeout=500; $port.NewLine="`n"
$watch=[Diagnostics.Stopwatch]::StartNew(); $lines=[Collections.Generic.List[object]]::new(); $partial=''
function Receive([int]$Milliseconds) {
 $end=$watch.ElapsedMilliseconds+$Milliseconds
 while($watch.ElapsedMilliseconds -lt $end) {
  $script:partial += $port.ReadExisting()
  while($script:partial.Contains("`n")) {
   $cut=$script:partial.IndexOf("`n"); $line=$script:partial.Substring(0,$cut).TrimEnd("`r"); $script:partial=$script:partial.Substring($cut+1)
   $lines.Add([pscustomobject]@{ms=$watch.ElapsedMilliseconds;direction='RX';line=$line})
  }
  Start-Sleep -Milliseconds 2
 }
}
function Send([string]$Line) { $lines.Add([pscustomobject]@{ms=$watch.ElapsedMilliseconds;direction='TX';line=$Line}); $port.WriteLine($Line) }
function WorkingSnapshot([int]$Id) {
 Send "BTPREVIEW1 GET 1 $Id 0"; Receive 600
 $cells=@($lines | Where-Object {$_.line -match "^BTPREVIEW1 CELL 2 $Id "} | ForEach-Object {
  $fields=$_.line.Split(' '); [pscustomobject]@{index=[int]$fields[4];note=[int]$fields[5];instrument=[int]$fields[6];timing=[int]$fields[7]}
 })
 $begin=$lines | Where-Object {$_.line -match "^BTPREVIEW1 BEGIN 2 $Id "} | Select-Object -Last 1
 if(!$begin) {throw 'No complete working snapshot'}
 $f=$begin.line.Split(' '); $rows=[int]$f[4]; $channels=[int]$f[5]
 if($cells.Count -ne $rows*$channels -or !($lines | Where-Object {$_.line -match "^BTPREVIEW1 END 2 $Id "})) {throw 'Incomplete snapshot'}
 return [pscustomobject]@{rows=$rows;channels=$channels;cells=$cells}
}
function Preview($Pattern,[uint32]$Id) {
 [uint64]$hash=2166136261
 foreach($b in @($Pattern.rows,$Pattern.channels)) {$hash=(($hash -bxor $b)*16777619)%4294967296}
 Send "BTPREVIEW1 BEGIN 1 $Id $($Pattern.rows) $($Pattern.channels) $($Pattern.cells.Count)"
 foreach($c in $Pattern.cells) {
  foreach($b in @($c.note,$c.instrument,$c.timing)) {$hash=(($hash -bxor $b)*16777619)%4294967296}
  Send "BTPREVIEW1 CELL 1 $Id $($c.index) $($c.note) $($c.instrument) $($c.timing)"
 }
 Send "BTPREVIEW1 COMMIT 1 $Id $($Pattern.cells.Count) $hash"; Receive 600
 if(!($lines | Where-Object {$_.line -eq "BTPREVIEW1 APPLIED 1 $Id"})) {throw "Preview $Id not applied"}
}
try {
 $port.Open(); Send 'BTTEST1 HELLO'; Receive 1200
 if(!($lines | Where-Object {$_.line -eq 'BTPREVIEW1 CAP 1'})) {throw 'Preview unavailable'}
 Send 'BTPATTERN1 GET 2 100'; Receive 600
 $original=WorkingSnapshot 101
 $initial=$lines | Where-Object {$_.line -like 'BTTEST1 STATE *'} | Select-Object -First 1
 $original | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $EvidenceDirectory 'working-before.json')
 Send 'BTTEST1 STOP'; Receive 200; Send 'BTTEST1 START'; Receive 250
 for($trial=0;$trial -lt 3;$trial++) {
  Send 'BTTEST1 STATUS'; Receive 200
  $next=$lines | Where-Object {$_.line -match '^BTPREVIEW1 NEXT 1 '} | Select-Object -Last 1
  [uint32]$id=$next.line.Split(' ')[3]
  $candidate=$original | ConvertTo-Json -Depth 5 | ConvertFrom-Json
  $cell=$candidate.cells | Where-Object {$_.note -ge 24 -and $_.note -le 126 -and $_.instrument -ne 255} | Select-Object -First 1
  if(!$cell) {throw 'No explicit pitched cell'}
  $cell.note+=1
  Preview $candidate $id
  Send 'BTTEST1 PAUSE'; Receive 200; Send 'BTTEST1 CONTINUE'; Receive 200
  Send 'BTTEST1 STATUS'; Receive 200
  $next=$lines | Where-Object {$_.line -match '^BTPREVIEW1 NEXT 1 '} | Select-Object -Last 1
  Preview $original ([uint32]$next.line.Split(' ')[3])
 }
 Send 'BTTEST1 STOP'; Receive 250
 $after=WorkingSnapshot 102
 $after | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $EvidenceDirectory 'working-after.json')
 [pscustomobject]@{initial_state=$initial.line;final_state='STOP requested; see STOPPED evidence';firmware_environment='existing flashed firmware, not rebuilt or uploaded';sample_audio_measured=$false;restored_original_working=($original | ConvertTo-Json -Depth 5 -Compress) -eq ($after | ConvertTo-Json -Depth 5 -Compress)} | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $EvidenceDirectory 'hardware-summary.json')
} finally {
 if($port.IsOpen) { $port.Close() }; $port.Dispose()
 $lines | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $EvidenceDirectory 'direct-serial-timing.json')
}
