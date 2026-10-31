@echo off
chcp 65001 >nul
cd /d "%~dp0"
powershell -NoProfile -Command "$d=Get-Date -Format yyyyMMdd; $f='docs/records/'+$d+'-record.md'; if(-not (Test-Path $f)){Copy-Item 'docs/records/_模板.md' $f; Write-Host ('Created: '+$f)} else {Write-Host ('Exists: '+$f)}; Start-Process notepad $f"
