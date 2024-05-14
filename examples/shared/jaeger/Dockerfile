FROM jaegertracing/all-in-one@sha256:9ff7eae5dbb8d543d35ad919ee1f162f74179901808badf7cb7f1c9327d7b8b1
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
