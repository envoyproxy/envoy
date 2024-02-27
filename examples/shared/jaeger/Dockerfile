FROM jaegertracing/all-in-one@sha256:909211d62e70c171281587e1f78fc0872a879233ac9f051d2e90a5905dc73b4c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
