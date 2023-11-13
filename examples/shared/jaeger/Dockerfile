FROM jaegertracing/all-in-one@sha256:ec208e2421066587a4383a79789ae00f998e205c975821310889323e6b0264f7
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
