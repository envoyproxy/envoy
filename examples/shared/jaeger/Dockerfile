FROM jaegertracing/all-in-one@sha256:2c4e89c970c3ffa6f013293c69e091c4c3c942fedfdac3ad6bf048ad58bda069
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
