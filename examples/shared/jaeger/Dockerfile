FROM jaegertracing/all-in-one@sha256:54551aae95d395a13508b367adc4a6e217e6deb84055046a790b8cd3e9cf4425
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
