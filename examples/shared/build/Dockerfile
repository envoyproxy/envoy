FROM envoyproxy/envoy-build-ubuntu:56f235b141079013e64912d676fe7da981368402@sha256:d44499c6fd28a8a6a75dc61668b8a9e7bc3d99db11f9a61e8ea1d1f39c20a236
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    # apt-get -qq update \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
