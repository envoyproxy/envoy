FROM jaegertracing/all-in-one@sha256:4906e3315fd3019c249d9095cdddb8bd53c56aac160ed4307093b2ef028c5387
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
