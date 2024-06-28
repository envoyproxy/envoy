FROM jaegertracing/all-in-one@sha256:3ca64bd528de89e11464984e1a0c3d4cae01bc8914470dd696b2eb6f643078f7
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
