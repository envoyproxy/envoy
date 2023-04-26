FROM jaegertracing/all-in-one@sha256:af062c6d0632f5bcf82a8a5572442fec2c79af7a780d689e9f1dbe17f4544fe2
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
