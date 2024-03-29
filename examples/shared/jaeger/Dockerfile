FROM jaegertracing/all-in-one@sha256:8559c8c99eb1bfeb4caf4ed96baa4fff55a64cbdcbf95a2eca6e617b765e4aa1
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
