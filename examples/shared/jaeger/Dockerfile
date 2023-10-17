FROM jaegertracing/all-in-one@sha256:537b6f32850f0e9228b9eb5bc2b1f9f6043b21f82cefe3a94149e98f76abc0d3
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
