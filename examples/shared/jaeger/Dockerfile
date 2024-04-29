FROM jaegertracing/all-in-one@sha256:b793d0795a5f2ed5c24a355f1296dd75645c6cf4607281ba8942994ac350c4c0
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
