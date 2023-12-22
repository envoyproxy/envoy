FROM jaegertracing/all-in-one@sha256:90be99e73e8eb2f6bda367f2fc9be22226e880668a950ce7c3647bf2310f0c8d
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
