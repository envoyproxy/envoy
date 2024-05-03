FROM jaegertracing/all-in-one@sha256:e1460b7dd95e8fc89602f08d4f11492516f7cde11f716b5b7796cf925f316acc
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
