FROM jaegertracing/all-in-one@sha256:8a0917e82a959d4fc1dad56a2dc0fa7a2f43b509f41c371ac979e3d08b9e7aa2
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
