FROM jaegertracing/all-in-one@sha256:10193a69b7b87a0d5205a44cf9f5979389d9b775b0a1430ba26a000c4c858e18
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
