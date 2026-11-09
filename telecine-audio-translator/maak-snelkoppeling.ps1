param(
  [ValidateSet("Bureaublad", "Opstarten")]
  [string]$Doel = "Bureaublad"
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$vbs = Join-Path $here "start.vbs"
$shell = New-Object -ComObject WScript.Shell
if ($Doel -eq "Opstarten") {
  $folder = [Environment]::GetFolderPath("Startup")
} else {
  $folder = [Environment]::GetFolderPath("Desktop")
}
$lnk = Join-Path $folder "TeleCine Audio Translator.lnk"
$sc = $shell.CreateShortcut($lnk)
$sc.TargetPath = Join-Path $env:SystemRoot "System32\wscript.exe"
$sc.Arguments = '"' + $vbs + '"'
$sc.WorkingDirectory = $here
$sc.WindowStyle = 1
$sc.Description = "TeleCine Audio Translator"
$sc.Save()
Write-Host "Snelkoppeling geplaatst: $lnk"
