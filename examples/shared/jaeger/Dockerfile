FROM jaegertracing/all-in-one@sha256:ba4535eb51f41527e27fa013ad47612533c5ce4ea3130b1d76a3d2aff7da1bcc
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
