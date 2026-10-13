param([string]$EvidenceDirectory)
$ErrorActionPreference='Stop'
if(Get-Process BroTrackerTerminal -ErrorAction SilentlyContinue) {throw 'Existing BTX instance; refusing second owner'}
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class BtxKeys {
 public delegate bool Callback(IntPtr h,IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback cb,IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,System.Text.StringBuilder text,int cap);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint k,uint t);
}
"@
$exe=(Resolve-Path deploy/windows/BroTrackerTerminal.exe).Path
$proc=Start-Process -FilePath $exe -WindowStyle Hidden -PassThru
$events=[Collections.Generic.List[object]]::new()
function Key([int]$Code,[bool]$Extended=$false,[bool]$Up=$false) {
 if([BtxKeys]::GetForegroundWindow() -ne $window) {throw 'BTX focus lost; abort injection'}
 [long]$bits=1+([long][BtxKeys]::MapVirtualKey($Code,0) -shl 16)
 if($Extended) {$bits=$bits -bor 0x01000000}
 if($Up) {$bits=$bits -bor 0xC0000000L}
 $events.Add([pscustomobject]@{utc=[DateTime]::UtcNow.ToString('o');key=$Code;extended=$Extended;up=$Up})
 if(![BtxKeys]::PostMessage($window, $(if($Up){0x101}else{0x100}),[IntPtr]$Code,[IntPtr]$bits)) {throw 'PostMessage failed'}
}
function Tap([int]$Code,[bool]$Extended=$false) {Key $Code $Extended; Key $Code $Extended $true}
try {
 Start-Sleep -Seconds 6; $proc.Refresh(); $script:window=[IntPtr]::Zero
 $callback=[BtxKeys+Callback]{param($h,$l) [uint32]$owner=0; [void][BtxKeys]::GetWindowThreadProcessId($h,[ref]$owner); if($owner -eq $proc.Id) {$name=[Text.StringBuilder]::new(128); [void][BtxKeys]::GetClassName($h,$name,128); if($name.ToString() -eq "SDL_app") {$script:window=$h; return $false}}; return $true}
 [void][BtxKeys]::EnumWindows($callback,[IntPtr]::Zero)
 if(!$window) {throw 'No BTX window'}
 [void][BtxKeys]::ShowWindow($window,5)
 [void][BtxKeys]::SetForegroundWindow($window); Start-Sleep -Milliseconds 200
 Tap 13; Start-Sleep -Milliseconds 500 # clean Return STOP
 Tap 32; Start-Sleep -Milliseconds 500 # START
 Tap 33; Start-Sleep -Seconds 2 # first valid edit
 Tap 33; Start-Sleep -Seconds 2 # second valid edit
 Tap 13; Start-Sleep -Milliseconds 500 # dirty Return STOP
 Tap 32; Start-Sleep -Milliseconds 500
 Tap 33; Tap 13 $true; Start-Sleep -Seconds 2 # keypad STOP near preview
 Tap 32; Start-Sleep -Milliseconds 500
 Key 33; Start-Sleep -Milliseconds 550; Tap 13; Key 33 $false $true; Start-Sleep -Seconds 2
 Tap 115; Start-Sleep -Seconds 2 # F4 restores original boot baseline here
 Tap 13 $true; Start-Sleep -Milliseconds 700
} finally {
 $events | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $EvidenceDirectory 'btx-window-message-input-v2.json')
 if(!$proc.HasExited) {[void][BtxKeys]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero); if(!$proc.WaitForExit(12000)) {throw 'BTX did not cleanly exit; do not open another serial owner'}}
 Copy-Item -LiteralPath 'C:/Users/smartin/AppData/Roaming/BroTracker/BroTracker Terminal/brotracker-terminal.log' -Destination (Join-Path $EvidenceDirectory 'btx-injected-input-v2.log')
 Get-FileHash $exe | Format-List | Out-String | Set-Content (Join-Path $EvidenceDirectory 'tested-executable-sha256.txt')
}
