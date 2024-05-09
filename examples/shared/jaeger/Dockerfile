FROM jaegertracing/all-in-one@sha256:7de98af6019905f962601b7e3553053d74f2e06e74af7fb844300f1de3ae7bb9
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
