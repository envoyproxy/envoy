FROM jaegertracing/all-in-one@sha256:5c755d9eb00ba9487d18f301973f55e88e30eba3fcf32306d92fde025dd3fd37
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
