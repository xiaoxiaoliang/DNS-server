# DNS-server deploy script (Windows -> Linux, rsync)
# Usage: .\deploy.ps1

$RemoteIP  = "192.168.9.23"
$RemoteDir = "/data/dns-srv/"

Write-Host "=== Syncing source to ${RemoteIP}:${RemoteDir} ===" -ForegroundColor Cyan

Push-Location $PSScriptRoot
rsync -avz --delete `
    --exclude='Build/' `
    --exclude='.git/' `
    --exclude='.vs/' `
    --exclude='*.sln' `
    --exclude='*.vcxproj*' `
    --exclude='*.user' `
    --exclude='*.ps1' `
    ./ `
    "root@${RemoteIP}:${RemoteDir}"
Pop-Location

Write-Host "=== Done ===" -ForegroundColor Green
