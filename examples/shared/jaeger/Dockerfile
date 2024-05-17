FROM jaegertracing/all-in-one@sha256:5f70d276f1e9fd05cfbd450bdbf7db6cb26d62f8f314752cd894e259b1fe7165
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
