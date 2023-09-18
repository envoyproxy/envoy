FROM jaegertracing/all-in-one@sha256:30238ffd383f266651cd4e0c36be67b6b0d3d882d0bbb67304c39af9ee61a4ef
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
