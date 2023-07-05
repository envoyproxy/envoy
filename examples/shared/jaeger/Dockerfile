FROM jaegertracing/all-in-one@sha256:fc726b48c644e68f2c76d6abe42550a1505427079ab11798ffe085d5b75d1abf
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
