FROM jaegertracing/all-in-one@sha256:2e237058ac38b1624607d4347dbff8a8e355ae650cb2c07353b02ade05802d29
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
