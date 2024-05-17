FROM envoyproxy/envoy-build-ubuntu:f29d90fdcd2e8e1b92f1eb7aa75d45fbb79d0d84@sha256:66ab4df63bba855b42bd4253356f2b703841590ab416799eb69b9c825b905b43
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
