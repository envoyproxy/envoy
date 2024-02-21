FROM jaegertracing/all-in-one@sha256:2c8e4a0bec794046d92d0487f9abc423c60ebf0e970b832eec1bbb623f11134a
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
