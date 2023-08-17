FROM jaegertracing/all-in-one@sha256:ab26b4f4364c7e59f6144fbbc9573fb82589cdd0703f1dda06a1d258ffc55d30
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
