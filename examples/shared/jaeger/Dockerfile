FROM jaegertracing/all-in-one@sha256:565de7777683cd97c04e490f4682ce99973851c1cdb01c05c24aed3168fdcc64
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
