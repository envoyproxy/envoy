FROM jaegertracing/all-in-one@sha256:07ab52e18d0a50a51d9d1a910939d2a76ff1a0c61928ea9792a20003128e7723
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
