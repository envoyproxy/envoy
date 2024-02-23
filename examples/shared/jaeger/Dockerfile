FROM jaegertracing/all-in-one@sha256:1ac0d023b8fe8101ec55f8ca10a9fd87426f4df9cb131d5e4c2e0e427df1d75d
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
