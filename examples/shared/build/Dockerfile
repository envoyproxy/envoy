FROM envoyproxy/envoy-build-ubuntu:f9138e5e0f7d555156e3d6583f8eda578af81aae@sha256:b705f5390ac725272d39f88c559c8914598fc1679324ee53e3ba45b24175dcd2
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
