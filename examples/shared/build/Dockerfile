FROM envoyproxy/envoy-build-ubuntu:e2ed613d194b37e30f9508bc4ff53b7f7065ad56@sha256:75fbea2ba80f2c6a8483b2dc5651a992fb76b7a0ab98c41c072807b9e2eb17f4
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
