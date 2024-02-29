FROM envoyproxy/envoy-build-ubuntu:a7e14c742e0968a6a55ff80e9f4a3d107d7fd71f@sha256:556d7362de123ddc09b26f407a174122a7df54f9ae7e70007f254fe9d0033197
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
