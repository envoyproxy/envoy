@ECHO OFF
bash -c "bazel/get_workspace_status %*"
exit %ERRORLEVEL%
