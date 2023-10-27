FROM jaegertracing/all-in-one@sha256:8d4b9369b071fe9b79264c1739c07ba798d623ee2927d45aae02b3041fd0de27
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
