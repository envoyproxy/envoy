FROM jaegertracing/all-in-one@sha256:2e78effd87fb1dec8a795f5fa84c5e97e34b8ae6f2b1c0221a3aa4842abb77b3
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
