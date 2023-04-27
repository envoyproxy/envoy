FROM jaegertracing/all-in-one@sha256:09daa1a109351ab569f696766d5c1d6752a20ccf32a4561178725cf5dc33e43f
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
