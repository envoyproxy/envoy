FROM jaegertracing/all-in-one@sha256:a50cc1ff337eeec12d0c0c4b85d5b3bff42fa2dd8d5b9c3cd8b836f2eafb7d1f
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
