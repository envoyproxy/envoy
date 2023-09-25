FROM jaegertracing/all-in-one@sha256:0f844f25690432f4c400bacf1bb681663dcbe1a8d864de054c27fdfe0b6da8fc
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
