FROM jaegertracing/all-in-one@sha256:94c7e31fcfc8ec019d6068f070bf72741fc52a493c9c9e5d742a58f28ce0307b
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
