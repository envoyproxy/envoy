FROM jaegertracing/all-in-one@sha256:842fa4392475814e24a247ca37ea8e88ed4121ce3ed68ea346e7f17ed283d8f0
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
