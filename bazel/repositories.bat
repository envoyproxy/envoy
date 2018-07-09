echo "Start"
@ECHO OFF
%BAZEL_SH% -c "./repositories.sh  %*"
exit %ERRORLEVEL%
