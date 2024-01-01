FROM jaegertracing/all-in-one@sha256:8c5da20e79e7ec3fec32ae7fc97e72f1e4b51ba5b372eb3d1aef4c896ca85c20
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
