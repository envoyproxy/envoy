FROM jaegertracing/all-in-one@sha256:9620b1b576e90706a9906d6c5a463fb7c2c59c458c43eeaa2338f09444d4e4e7
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
