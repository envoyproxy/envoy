FROM jaegertracing/all-in-one@sha256:cfdd0e350ca29aaacc4ae0b2c946c2ef3a33853edd354a15b98e7bb611323099
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
