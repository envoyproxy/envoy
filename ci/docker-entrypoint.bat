@echo off
setlocal

set CMD=%*%

REM if the first argument look like a parameter (i.e. start with '-'), run Envoy
set first_arg=%1%
if "%first_arg:~0,1%" == "-" (
    set CMD=envoy.exe %CMD%
)

if /i "%1" == "envoy" set is_envoy=1
if /i "%1" == "envoy.exe" set is_envoy=1
if defined is_envoy (
    REM set the log level if the $loglevel variable is set
    if defined loglevel (
        set CMD=%CMD% --log-level %loglevel%
    )
)

%CMD%
