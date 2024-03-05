FROM jaegertracing/all-in-one@sha256:74438d98a1abb13366ae22fb798cafedc9bb11c5a6833f4a6b94ec1015899acb
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
