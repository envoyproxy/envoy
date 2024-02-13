FROM jaegertracing/all-in-one@sha256:42460d5a7884c627948106d0030fda351dbe480c91c260d19b5da1cb0fd4db06
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
