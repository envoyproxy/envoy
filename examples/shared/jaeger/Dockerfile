FROM jaegertracing/all-in-one@sha256:38f5cc402e92a8bef212536739e8041aa5dd5e2f0b78a042504ac20fb84bffa7
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
