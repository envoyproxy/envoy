FROM jaegertracing/all-in-one@sha256:826d8f43a66c934ac8ba3d90252abdd1ad3a00f6deb895af817ed3b137ec191e
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
