FROM jaegertracing/all-in-one@sha256:0193509cb29eef105d0d22bcebc89551fdb29d5e363ea20cfced9afe9c403f06
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
