FROM jaegertracing/all-in-one@sha256:6f9f2b9e096b4bc364f2c8d6fa3dffa4a4dcce3aff1cf052a592356ca836033d
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
