FROM jaegertracing/all-in-one@sha256:0d02dcd9202e53472e215dca58f93f9fbe24c53db96bcdfc702502489d4a80dc
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
