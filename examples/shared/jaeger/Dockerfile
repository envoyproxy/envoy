FROM jaegertracing/all-in-one@sha256:16f1fbdd62bf2567da0b9d33ac4b95040ffad00114d58fe70cee064b174e3c4d
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
