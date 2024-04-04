FROM jaegertracing/all-in-one@sha256:d2cd4c226624bdc116decd3106091b4df9882da8db42f8550293596cab79b8ea
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
