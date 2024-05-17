FROM envoyproxy/envoy-build-ubuntu:ae0f60a5434a0f4e6448e0370e08b21ab5bda396@sha256:eb999b81d1c78128f3dfb3ac7b8798f6a2444c4e1141655f3a92daa62da2f7b9
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
