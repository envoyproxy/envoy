FROM jaegertracing/all-in-one@sha256:943952393d0bdeb06355beefdb9846e3f79ace4783fde0b270aed47c76b349d7
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
