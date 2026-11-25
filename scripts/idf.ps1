param([Parameter(ValueFromRemainingArguments = $true)][array]$Cmd)
# Thin wrapper so WSL can drive idf.py without hand-writing the profile invocation.
& 'C:\Espressif\tools\Microsoft.v6.0.3.PowerShell_profile.ps1'
Set-Location (Join-Path $PSScriptRoot '..\esp')
& idf.py @Cmd
exit $LASTEXITCODE
