FROM jaegertracing/all-in-one@sha256:b22e9b1913536fcf832a60a1936dd823dc457f790d36a0092c385d9dbd9092b0
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
