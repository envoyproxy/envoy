FROM jaegertracing/all-in-one@sha256:e050fc3a7020fb9e4ae8e9bf70d7f1ef0cb480ea0ecefdfc8e39d0e126b0fae2
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
