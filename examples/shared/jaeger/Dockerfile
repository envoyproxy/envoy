FROM jaegertracing/all-in-one@sha256:5cce963664f3a88ad9e8f122e6be2bf6085886b14f0440f3b7ac1e59329485f3
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
