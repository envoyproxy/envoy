FROM jaegertracing/all-in-one@sha256:8cdd1c033aa3b8512c2e60d50e203b8a970779019ce0b18288e9f83d39e0806a
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
