FROM jaegertracing/all-in-one@sha256:361f5be7705724b95fbd19cf0a8da0f500ffcbd00ba88828220294d9f833a7d1
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
