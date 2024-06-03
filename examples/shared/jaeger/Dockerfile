FROM jaegertracing/all-in-one@sha256:177998a612bd025a5ed80d020392ff51fdfae586df58fc8970e419266e1bd7a8
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
