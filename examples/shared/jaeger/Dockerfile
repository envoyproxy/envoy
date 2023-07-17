FROM jaegertracing/all-in-one@sha256:57398ead39857e10469a952aee163685af3396a40926a3e7da8d55e982a00a70
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
