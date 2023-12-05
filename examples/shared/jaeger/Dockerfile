FROM jaegertracing/all-in-one@sha256:762d30bd34aa10f78c80138a7e99190b1cef91d95a584a6916c072fab3254497
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
