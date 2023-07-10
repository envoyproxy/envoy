FROM jaegertracing/all-in-one@sha256:3aa5c114d8bad74b199d19d315162a176d35af375e79e8bc68038c3c33ab3f39
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
