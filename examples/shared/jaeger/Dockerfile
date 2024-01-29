FROM jaegertracing/all-in-one@sha256:99d07e054d9bf7d4f78555972eb0b14e9fd133dbe9cb0d630ed2431eb5846ce6
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
