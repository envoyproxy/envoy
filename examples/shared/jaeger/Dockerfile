FROM jaegertracing/all-in-one@sha256:9c75fc21ad3691e5bb882672e38c3bd3eacd9b97b8ad8239e60a6fc578bef66a
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
