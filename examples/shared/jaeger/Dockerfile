FROM jaegertracing/all-in-one@sha256:a223a78c106c02c6c7202f62a308ece44744f2357bd77f6d69ff471ae91fe1b0
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
