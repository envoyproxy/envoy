FROM jaegertracing/all-in-one@sha256:7fec93abbe47ead28398c9fa78de5151c5b831a81d9f784b9dd112255449f2fb
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
