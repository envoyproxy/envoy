FROM jaegertracing/all-in-one@sha256:08e97a8d1042daa1a407e66d223af179b410422c53693eb30b171b0b051f651f
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
