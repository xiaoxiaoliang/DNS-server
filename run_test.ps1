# Batch run test.bat (no window)
# Usage: .\run_test.ps1 [-Count N] [-Delay S]

param(
    [int]$Count = 10,
    [int]$Delay = 0
)

Write-Host "=== Starting $Count test.bat instances ===" -ForegroundColor Cyan

1..$Count | ForEach-Object {
    Start-Process -FilePath "cmd.exe" -ArgumentList "/c test.bat" -WindowStyle Hidden
    Write-Host "  [$_] test.bat started" -ForegroundColor Green
    if ($Delay -gt 0) { Start-Sleep -Seconds $Delay }
}

Write-Host "=== Done ===" -ForegroundColor Green
