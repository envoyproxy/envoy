FROM jaegertracing/all-in-one@sha256:cbf88e97528af5f4d659d51d176063db99664a542bac8d173c257bcb82fca58f
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
