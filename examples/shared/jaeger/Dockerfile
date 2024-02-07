FROM jaegertracing/all-in-one@sha256:f4044964505c81bf12131c2dfd1f4ddf1d1421bbbd9d79f691221cac077599a8
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
