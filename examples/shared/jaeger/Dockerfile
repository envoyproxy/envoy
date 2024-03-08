FROM jaegertracing/all-in-one@sha256:cac296f4437b7b01f572001f0936944969a903f2da9f861311c38aed296b8824
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
