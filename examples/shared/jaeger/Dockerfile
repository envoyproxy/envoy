FROM jaegertracing/all-in-one@sha256:50974de35799552df81e982c26f3bf8d3e16811fcf45cf51eb03575922ceff55
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
