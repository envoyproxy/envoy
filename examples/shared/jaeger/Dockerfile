FROM jaegertracing/all-in-one@sha256:e65bb6166703f6c3b1cc9a11d96640f69b3ae46d40e7c386ca3b06814223662e
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
