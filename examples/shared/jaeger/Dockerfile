FROM jaegertracing/all-in-one@sha256:738a2c24b533d1f011c4e3549e53835ce1013f489b6dbba2eb0b5072711a53dc
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
