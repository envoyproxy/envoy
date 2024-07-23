FROM jaegertracing/all-in-one@sha256:00520722e3272c8e33c6ad871760c1fdeae829f04f97ca166d8c0070e01cd204
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
