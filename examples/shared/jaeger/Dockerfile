FROM jaegertracing/all-in-one@sha256:b68bc4fde39a783f59a155bcc6687051179767445ef66087f62bc883a257d0a7
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
