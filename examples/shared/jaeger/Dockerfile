FROM jaegertracing/all-in-one@sha256:5672cdad35fcf1e56446789d8183a07502452ee990583db173d08f694ba193b4
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
