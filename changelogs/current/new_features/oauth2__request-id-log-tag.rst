The OAuth2 filter now includes a ``RequestId`` tag in its application log lines, matching the
access log ``%STREAM_ID%`` / ``x-request-id`` value, so operators can correlate OAuth2
application logs with access logs on a per-request basis.
