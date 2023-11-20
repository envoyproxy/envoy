FROM jaegertracing/all-in-one@sha256:7da619b0d194d2cfc3572da5e0e7074956815b17d5dfe4393e42b3c2a565aeb9
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
