@echo off
setlocal
rem Runs the %exe%.exe located next to this script, with the caller's
rem arguments, in mintty on the bundled runtime in ..\runtime\, with the
rem program's settings and log in %exe%\ and mintty's in mintty\ of this
rem folder; the window stays open after an error.
if not defined exe (
  echo Intended to be run from a caller script which sets the `exe` variable.
  pause
  exit /b 1
)
set "exe_dir=%~dp0"
set "runtime_bin=%exe_dir%..\runtime\usr\bin"
set "PATH=%runtime_bin%;%PATH%"
set "exe_dir_without_trailing_backslash=%exe_dir:~0,-1%"
set "exe_dir_with_forward_slashes=%exe_dir_without_trailing_backslash:\=/%"
set "XDG_CONFIG_HOME=%exe_dir_with_forward_slashes%"
set "XDG_STATE_HOME=%exe_dir_with_forward_slashes%"
start "" "%runtime_bin%\mintty.exe" --configdir "%exe_dir%mintty" --hold error --title "%exe%" "%exe_dir%%exe%.exe" %*
