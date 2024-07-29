FROM jaegertracing/all-in-one@sha256:37e5bf17697465f9704d3094215d670c0b47585e3fbe60e6f1e8e7f6712b484c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
