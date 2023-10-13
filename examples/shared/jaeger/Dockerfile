FROM jaegertracing/all-in-one@sha256:645efbc21dccd6f5c0ff63ccedd894caab01470d6148da6473c46fa793826c48
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
