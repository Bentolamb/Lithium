@echo off
rem Starts Lithium without an installer or an unsigned .exe: a tiny local server plus an Edge/Chrome app window.
setlocal
cd /d "%~dp0"
set NODE=
where node >nul 2>nul && set NODE=node
if not defined NODE if exist "%ProgramFiles%\nodejs\node.exe" set NODE="%ProgramFiles%\nodejs\node.exe"
if not defined NODE if exist "C:\dev\emsdk\node\24.19.0_64bit\node.exe" set NODE="C:\dev\emsdk\node\24.19.0_64bit\node.exe"
if not defined NODE (
  echo Node.js is needed to run this. Install it from https://nodejs.org and run this file again.
  pause & exit /b 1
)
start "" /b %NODE% serve-gui.js 47653 >nul 2>nul
set BROWSER=
if exist "%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe" set BROWSER="%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
if not defined BROWSER if exist "%ProgramFiles%\Google\Chrome\Application\chrome.exe" set BROWSER="%ProgramFiles%\Google\Chrome\Application\chrome.exe"
if not defined BROWSER (start "" http://localhost:47653/ & echo Close this window to stop Lithium. & pause >nul & goto stop)
rem a private profile keeps Lithium's saved lists in one place and lets us wait for the window to close
%BROWSER% --app=http://localhost:47653/ --user-data-dir="%LOCALAPPDATA%\Lithium\profile" --no-first-run --window-size=1360,860 --disable-features=msEdgeSidebarV2
:stop
taskkill /f /fi "WINDOWTITLE eq serve-gui*" >nul 2>nul
for /f "tokens=5" %%p in ('netstat -ano ^| findstr ":47653 " ^| findstr LISTENING') do taskkill /f /pid %%p >nul 2>nul
