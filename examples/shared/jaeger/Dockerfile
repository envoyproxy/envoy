FROM jaegertracing/all-in-one@sha256:7eac7a9e6fdb96ea5965e750b3e9dfc97bff195289a0a6cfb9560eb31f9d2c34
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
