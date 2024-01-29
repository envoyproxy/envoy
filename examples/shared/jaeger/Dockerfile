FROM jaegertracing/all-in-one@sha256:ead304a221a110b26b54bed36129ee48eaac67f1b68b733b40dc9fde8039c513
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
