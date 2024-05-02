FROM jaegertracing/all-in-one@sha256:899d755c29a10698ff8ed8f674a2c13baf371d9610a3fa7e6d420f5603078ca6
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
