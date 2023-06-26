FROM alpine:3.18@sha256:82d1e9d7ed48a7523bdebc18cf6290bdb97b82302a8a9c27d4fe885949ea94d1 as otelc_curl
RUN apk --update add curl

FROM otel/opentelemetry-collector:latest@sha256:51e6d8edd1ed33d328af83d5b99bd5551a034959c71bd81a973f71d21133bc7e

COPY --from=otelc_curl / /

COPY ./otel-collector-config.yaml /etc/otel-collector-config.yaml
USER 0
RUN chmod o+r /etc/otel-collector-config.yaml
USER nobody
