FROM jaegertracing/all-in-one@sha256:c2b9283bda7610416f23c835b74f96a50ada67d4af6b574d4f8cf01715d577b1
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
