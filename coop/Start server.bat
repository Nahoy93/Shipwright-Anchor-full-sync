@echo off
rem ============================================================
rem  Anchor server for Ship of Harkinian (self-hosted)
rem  Change the password below before starting!
rem ============================================================
cd /d "%~dp0"

set ANCHOR_PASSWORD=change-to-your-password
set PORT=43383

echo Starting the Anchor server on port %PORT% ...
echo Type "list" to see rooms and players, "stop" to close it.
echo.
anchor.exe
pause
