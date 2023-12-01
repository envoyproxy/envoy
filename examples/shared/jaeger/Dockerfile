FROM jaegertracing/all-in-one@sha256:412f505de981545411af742c85fe32aac8dae71092159b36d827fe0dff26cd21
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
