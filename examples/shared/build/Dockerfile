FROM envoyproxy/envoy-build-ubuntu:fdd65c6270a8507a18d5acd6cf19a18cb695e4fa@sha256:3c8a3ce6f90dcfb5d09dc8f79bb01404d3526d420061f9a176e0a8e91e1e573e
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    # apt-get -qq update \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
