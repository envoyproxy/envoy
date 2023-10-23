FROM jaegertracing/all-in-one@sha256:163604559823d468a39292925d52a6f15c15af8e3a4bb87e29a535726c7595f4
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
