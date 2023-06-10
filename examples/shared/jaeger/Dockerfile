FROM jaegertracing/all-in-one@sha256:37030876f8e4d42ed793c545b9420afebc46d9fb303affcec46c4b60b97e3c4a
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
