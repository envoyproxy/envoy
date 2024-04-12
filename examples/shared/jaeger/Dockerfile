FROM jaegertracing/all-in-one@sha256:7471c62dac0a28e6da27df7a86776fc4f8d9f414613ba025e76843a1fc9de150
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
