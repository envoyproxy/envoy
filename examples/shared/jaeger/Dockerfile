FROM jaegertracing/all-in-one@sha256:58f6cc4d9d691963fa971c65178d15631c0b714389ebb1fd80f542ce17e11784
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
