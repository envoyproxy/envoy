FROM ubuntu:16.04

RUN apt-get update \
    && apt-get upgrade -y \
    && apt-get install -y ca-certificates \
    && apt-get autoremove -y \
    && apt-get clean \
    && rm -rf /tmp/* /var/tmp/* \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /etc/envoy

ADD build_release_stripped/envoy /usr/local/bin/envoy
ADD configs/google_com_proxy.v2.yaml /etc/envoy/envoy.yaml

EXPOSE 10000

COPY ci/docker-entrypoint.sh /
ENTRYPOINT ["/docker-entrypoint.sh"]
CMD ["envoy", "-c", "/etc/envoy/envoy.yaml"]
