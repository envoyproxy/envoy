FROM jaegertracing/all-in-one@sha256:37172763bf48a5513e2eba356be7e2f29768d74055f8ff7e224c70cb6ed34ffc
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
