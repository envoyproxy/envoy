FROM jaegertracing/all-in-one@sha256:8fbb512660b7afc031127f16c8df94135195db5bec2643bf0c20036495f762da
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
