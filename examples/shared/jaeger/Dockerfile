FROM jaegertracing/all-in-one@sha256:ffb18df409052b106f048abefd5b87ab539c30135170ea88c856d016140b7e92
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
