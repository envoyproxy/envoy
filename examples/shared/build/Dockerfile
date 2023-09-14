FROM envoyproxy/envoy-build-ubuntu:1f2f7ee78f894859de0fa7a415b0bedde1f6c560@sha256:7b0d23792253a56ce8ba17b0781f41d1d8af8dbb19c9a19b7e485f634abfd22e
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    # apt-get -qq update \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
