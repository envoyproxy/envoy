FROM jaegertracing/all-in-one@sha256:7497260c50489e1a48bf4ac57e709fa13e874daabbc7e48213c13c917ba07b08
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
