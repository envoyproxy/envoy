FROM jaegertracing/all-in-one@sha256:e99a4b55b926de7e6c1b15ea08f26832fda045547bb75816f759419179104d12
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
