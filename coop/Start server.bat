@echo off
rem ============================================================
rem  Anchor server for Ship of Harkinian (self-hosted)
rem  Change the password below before starting!
rem ============================================================
cd /d "%~dp0"

set ANCHOR_PASSWORD=change-to-your-password
set PORT=43383

rem Encrypted connections (TLS) are on by default. The certificate is created in the tls folder on the
rem first start, keep that folder. Remove "rem" on a line below to change it:
rem set ANCHOR_REQUIRE_TLS=1
rem set ANCHOR_TLS=0
rem Movement over UDP is on by default (forward port %PORT% for both TCP and UDP):
rem set ANCHOR_UDP=0

echo Starting the Anchor server on port %PORT% ...
echo Type "list" to see rooms, players, ping and traffic, "stop" to close it.
echo.
anchor.exe
pause
