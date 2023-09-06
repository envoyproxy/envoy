FROM jaegertracing/all-in-one@sha256:8f090c17b7e5886d37395f8fc80726651d8adf6bfbcf5505b07adb0c6f1ffe0f
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
