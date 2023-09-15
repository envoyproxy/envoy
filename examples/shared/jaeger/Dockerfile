FROM jaegertracing/all-in-one@sha256:d3f0e1bb8d889abb5d3024f804b187d2639ea49c0471ff88cd2ee9761b6f875c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
