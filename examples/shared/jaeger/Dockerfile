FROM jaegertracing/all-in-one@sha256:390c92386d4a05176e1c5a7de5364ed3247e0c9bf14a59fc2202d8187c0c780c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
