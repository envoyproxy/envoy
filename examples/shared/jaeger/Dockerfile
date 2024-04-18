FROM jaegertracing/all-in-one@sha256:4e6a372702c18d3e2c1eb7c51df7d0bcab309118efa0d5f5da6a42b58a67cb14
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
