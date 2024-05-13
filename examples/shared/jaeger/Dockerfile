FROM jaegertracing/all-in-one@sha256:21f009a13e205937d0796090dcd8158d69f9687d7ae04e5ee83276c76b6fad6e
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
