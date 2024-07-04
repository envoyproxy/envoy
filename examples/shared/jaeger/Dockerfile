FROM jaegertracing/all-in-one@sha256:eb0230728f058b538284b7f8ece7f12ee5cd2fb2b04204d1c6b8e6c3a704ad40
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
