FROM jaegertracing/all-in-one@sha256:af0e250174b21d8f62001c2bfb7b99691db1816662e1c21ea9f6f5145c5804af
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
