FROM jaegertracing/all-in-one@sha256:521657055d8b608516d36a98583f8d8e38196599271c2547afee941ed5ac2e81
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
