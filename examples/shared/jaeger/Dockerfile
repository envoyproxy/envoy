FROM jaegertracing/all-in-one@sha256:8ea1b98341325a8078776094180bd405bd7c17b6b4b75b5e62fe030ecd69e491
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
