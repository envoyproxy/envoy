FROM jaegertracing/all-in-one@sha256:a8749c727049964106f2fb060c451d673919fc1e8e28f57f93bfccd319c3d61a
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
