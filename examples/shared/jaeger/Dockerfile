FROM jaegertracing/all-in-one@sha256:dd79a8dcf45a48e85b419d19559f7448ad83ea6f4d631b8f361b52686b3e5f72
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
