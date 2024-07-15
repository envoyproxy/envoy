FROM jaegertracing/all-in-one@sha256:af17a39bf9e720f623a940df4a0ab079f360ed1a312f207d4b842186e1459637
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
