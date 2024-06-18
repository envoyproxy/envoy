FROM jaegertracing/all-in-one@sha256:8832ee3af2c6c5c6de440900eacaf68fec5643f36d0145a8bd5495eea865a115
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
