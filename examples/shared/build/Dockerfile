FROM envoyproxy/envoy-build-ubuntu:fdd65c6270a8507a18d5acd6cf19a18cb695e4fa@sha256:06d3d10a99cce5bf4036be65190f192a30503fa93b9df3c119fd1260d3ed7024
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    # apt-get -qq update \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
