FROM jaegertracing/all-in-one@sha256:d87b1680c60f9ed7c627ad58f48f4ae78cc2eb3c65e5e933e093d20bf08bc22d
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
