FROM jaegertracing/all-in-one@sha256:82176fb65b133092521f9193810c88e8c0cccf8af2887032f4823eb33e207dad
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
