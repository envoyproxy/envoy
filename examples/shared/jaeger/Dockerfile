FROM jaegertracing/all-in-one@sha256:c4c57e6979156bd27b135ee546cd40d560cd3b90286d6829dfe19430a240d1d4
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
