FROM jaegertracing/all-in-one@sha256:cfeb44d62f721e8f170ba5ad1dcb58239f746c2fc8c5850053dddaa2a34b0e09
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
