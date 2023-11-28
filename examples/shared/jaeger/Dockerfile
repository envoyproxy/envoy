FROM jaegertracing/all-in-one@sha256:f3b50ce7daf97b1f83dcecd9a250a8772fa2ad8fd9d3e03b905400b33765b9c8
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
