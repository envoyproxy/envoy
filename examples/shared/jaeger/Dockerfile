FROM jaegertracing/all-in-one@sha256:38fdd8a7ad6d1de5b183540a517b73b6f0e60df5e44345592d5e823031b87011
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
