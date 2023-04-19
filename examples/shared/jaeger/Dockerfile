FROM jaegertracing/all-in-one@sha256:98f01272f33af82188d2b847c00936e9673073b0d0d1d3de5dd79b95ffac04c0
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
