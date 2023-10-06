FROM jaegertracing/all-in-one@sha256:dbf208b1dd4ff2e7dbcf11b8d5e4fb8a7fd2e716092630bef5546740fb5c9cbd
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
