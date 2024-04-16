FROM jaegertracing/all-in-one@sha256:b42e125338a46f85136de3f1d5f21fa385bba598e8429cef88ce534229e440fc
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
