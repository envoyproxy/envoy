FROM jaegertracing/all-in-one@sha256:0bfdc7a447c44330c97088ff634daaa718368de80b08fab0eea5a19e8f3a043d
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
