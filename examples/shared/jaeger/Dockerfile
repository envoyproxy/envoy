FROM jaegertracing/all-in-one@sha256:f2da966b340cea6aec5770425dd6fa096dc6fc16ead72e15b40a9d52b5e90e92
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
