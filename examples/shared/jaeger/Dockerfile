FROM jaegertracing/all-in-one@sha256:420aeec678b4313e2d6797b31c537a7ba996078ae6f4763b54d5b62deb140243
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
