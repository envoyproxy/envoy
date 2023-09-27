FROM jaegertracing/all-in-one@sha256:2cc6ae32a9533bc5f067391660a18192d0d0a98e8b0fa4ffcff7027ea87164ae
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
