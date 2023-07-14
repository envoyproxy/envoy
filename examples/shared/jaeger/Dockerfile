FROM jaegertracing/all-in-one@sha256:f88901c4f96e9ec592647aca2d455247b073ccfdfb6514dcbb2fca8579482150
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
