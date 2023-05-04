FROM jaegertracing/all-in-one@sha256:ac83e8d52ce8365390beda54de65235d392077da6e1e35f8b848f95db7e9d6b0
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
