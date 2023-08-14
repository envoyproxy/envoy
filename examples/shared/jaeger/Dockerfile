FROM jaegertracing/all-in-one@sha256:cf6adf82ec308b50506b05c02015562366234b3770315d878d1023bdecc6c04b
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
