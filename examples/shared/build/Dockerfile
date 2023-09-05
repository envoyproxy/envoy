FROM envoyproxy/envoy-build-ubuntu:1f2f7ee78f894859de0fa7a415b0bedde1f6c560@sha256:6a6a64be3f4e3c4380531ad1624f9419d1fe99dde4e5eeb04e2d538a92f27205
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    # apt-get -qq update \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
