FROM jaegertracing/all-in-one@sha256:2d5f2a50b3845301c619eeff38cd5cac77a38db1989f96accb12d2c1feeadeb0
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
