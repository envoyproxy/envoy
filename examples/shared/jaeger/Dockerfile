FROM jaegertracing/all-in-one@sha256:335af76f691353ab8da36f03f8adeab3db4a9a4641c8c59384022cd0aa5721d7
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
