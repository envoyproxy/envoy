FROM jaegertracing/all-in-one@sha256:963fed00648f7e797fa15a71c6e693b7ddace2ba7968207bb14f657914dac65b
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
