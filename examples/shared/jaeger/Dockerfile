FROM jaegertracing/all-in-one@sha256:b6615cfa31aa05edd491d9b75613dfc06572ee1a6f90dd7e9131b2f63c70a7f3
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
