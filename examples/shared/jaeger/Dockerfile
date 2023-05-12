FROM jaegertracing/all-in-one@sha256:a34185d2d040d44788580359ec9bffac794c4ec3e1c83b1da9b1448e9c8185a7
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
