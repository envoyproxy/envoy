FROM jaegertracing/all-in-one@sha256:e873ca64d234384b4be8e2efa88f6edbb6b8ef62298cf212b11d1ac2baded851
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
