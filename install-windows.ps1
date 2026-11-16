param([string]$Destination = "$env:LOCALAPPDATA\Programs\m1zuki")
$ErrorActionPreference = 'Stop'
$source = if (Test-Path "$PSScriptRoot\m1zuki.exe") { $PSScriptRoot } else { "$PSScriptRoot\dist\m1zuki-windows" }
if (!(Test-Path "$source\m1zuki.exe")) { throw 'Build the Windows package first.' }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
if ([IO.Path]::GetFullPath($source).TrimEnd('\') -ne [IO.Path]::GetFullPath($Destination).TrimEnd('\')) {
    Copy-Item -Path "$source\*" -Destination $Destination -Recurse -Force
}
$shell = New-Object -ComObject WScript.Shell
$programs = [Environment]::GetFolderPath('Programs')
$shortcut = $shell.CreateShortcut((Join-Path $programs 'm1zuki.lnk'))
$shortcut.TargetPath = Join-Path $Destination 'm1zuki.exe'
$shortcut.WorkingDirectory = $Destination
$shortcut.Save()
Write-Host "Installed: $Destination\m1zuki.exe"
