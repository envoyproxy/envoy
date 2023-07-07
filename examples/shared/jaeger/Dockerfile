FROM jaegertracing/all-in-one@sha256:ac85f812596ffb596ddcdbfe7c287eb44f781706e4232741bf9f81ff23aa1da9
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
