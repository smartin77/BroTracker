$ErrorActionPreference='Stop'
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class BtxFind {
 public delegate bool Callback(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback cb,IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
"@
$taskProcess=Get-Process BroTrackerTerminal -ErrorAction Stop
$callback=[BtxFind+Callback]{param($h,$l) [uint32]$owner=0; [void][BtxFind]::GetWindowThreadProcessId($h,[ref]$owner); if($owner -eq $taskProcess.Id) {Write-Host "Closing owned window $h"; [void][BtxFind]::PostMessage($h,0x10,[IntPtr]::Zero,[IntPtr]::Zero)}; return $true}
[void][BtxFind]::EnumWindows($callback,[IntPtr]::Zero)
if(!$taskProcess.WaitForExit(8000)) {throw 'BTX window close not completed; serial remains owned'}
