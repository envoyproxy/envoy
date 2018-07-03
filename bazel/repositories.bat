echo "Start"
@ECHO OFF
bash -c "./repositories.sh  %*"
exit %ERRORLEVEL%
