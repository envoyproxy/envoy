FROM jaegertracing/all-in-one@sha256:8f70b60c20c6495f8cfe2bfc5aef15c38ff37b14778b51048745852b4a203fdb
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
