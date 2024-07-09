FROM jaegertracing/all-in-one@sha256:063f8d8205824a8cff6d8337ac111e0e6af7663a8c078b88a0d65ca6dea42f43
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
