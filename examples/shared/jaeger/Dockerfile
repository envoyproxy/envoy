FROM jaegertracing/all-in-one@sha256:d0a97f79efec8e4e9513e2bea9345792b5b90a3e92240775831d328df4ca4ccf
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
