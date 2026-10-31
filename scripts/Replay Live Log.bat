@echo off
title AwowLab - Replay Live Log

rem Double-click launcher for tools/analysis/replay-live-log.py.
rem
rem Dumps the saved Midnight Falls pull into the WoW Logs folder in one write,
rem which is what the game does - the log is flushed when the encounter ends,
rem not streamed event by event. Live mode picks it up on its own.
rem
rem The replay is left in place afterwards. It has to outlive this script: the
rem app polls every couple of seconds, so deleting on exit meant the file could
rem be gone before it was ever read. Take it out by hand when done - the name
rem is printed below, and it is the only WoWCombatLog dated today.
rem
rem Everything is fixed on purpose - this is a test harness, not a tool. Edit
rem the three values below to replay something else.

cd /d "%~dp0.."

set "SOURCE=testlogs\midnight-falls-kill.txt"
set "OUTDIR=C:\Program Files (x86)\World of Warcraft\_retail_\Logs"
set "LOOPS=1"

rem The py launcher ships with the python.org installer and is the reliable way
rem to find an install; plain "python" on Windows may be the Store stub that
rem opens a shop page instead of running anything.
set "PY="
py -3 --version >nul 2>&1 && set "PY=py -3"
if not defined PY python --version >nul 2>&1 && set "PY=python"

if not defined PY (
    echo   Python was not found. Install it from python.org and try again.
    echo.
    pause
    exit /b 1
)

if not exist "%SOURCE%" (
    echo   Missing %SOURCE%
    echo.
    echo   That is the isolated pull the replay reads. Cut one out of a raid
    echo   log with the ENCOUNTER_START/ENCOUNTER_END line range.
    echo.
    pause
    exit /b 1
)

rem "%OUTDIR%" holds (x86), and an unquoted echo of it inside a parenthesised
rem block ends the block at the first ")". Hence the goto rather than an
rem if-block body, and the quotes on the echo below.
if not exist "%OUTDIR%" goto :nologs
goto :haslogs

:nologs
echo   The WoW Logs folder is not where this expects it:
echo   "%OUTDIR%"
echo.
echo   Edit OUTDIR at the top of this file to match your install.
echo.
pause
exit /b 1

:haslogs

echo.
echo   Start AwowLab and choose Watch Live FIRST, then come back here.
echo   The app has to be watching before the pull lands, the same way it
echo   would be during a raid.
echo.
pause

echo.
echo   Dumping %SOURCE% into the WoW Logs folder...
echo.

%PY% "tools\analysis\replay-live-log.py" "%SOURCE%" --out-dir "%OUTDIR%" --dump --loop %LOOPS%

echo.
echo   Written. The app should pick it up within a couple of seconds.
echo.
echo   Delete that file from the Logs folder when you are done - it is left
echo   in place on purpose, because the app needs to still be able to read it.
echo.
pause
