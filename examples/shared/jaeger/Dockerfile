FROM jaegertracing/all-in-one@sha256:1450fd0c9cc65b45c40472cbb4a21e1f2435256c9e8c2ee1dd698017bf4f78fe
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
