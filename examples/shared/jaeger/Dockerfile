FROM jaegertracing/all-in-one@sha256:aa3d8eca6edeb84d997af6712b62fc5db8e6217277f95c109d121ea7fe03c291
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
