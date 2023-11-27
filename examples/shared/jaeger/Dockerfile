FROM jaegertracing/all-in-one@sha256:4fb444cf147cec9b8fe7b1656aec602413a1c0b1b17f227ba3a7e23e7bf1fd3e
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
