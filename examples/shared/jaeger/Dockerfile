FROM jaegertracing/all-in-one@sha256:d2df9de095c3a7290919d8bd29f13aaf98a36a674e4a38b2a7a1247df8a0de69
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
