FROM jaegertracing/all-in-one@sha256:f543abf41c3d88d2f0f3789a8b6b6b9fdba52f5fe0e40932920b64fe92994fbd
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
