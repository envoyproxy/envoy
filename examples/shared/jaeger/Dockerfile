FROM jaegertracing/all-in-one@sha256:4a7679e70bf13065598b8500d21bd1042cb8a3df4306d9baaf1b52feb69922be
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
