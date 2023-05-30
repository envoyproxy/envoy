FROM jaegertracing/all-in-one@sha256:114816a5808fddda2bdcf85f335d306b279b2cebb1e48d9d1d0f0e9ab53beeb9
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
