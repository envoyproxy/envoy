FROM envoyproxy/envoy-build-ubuntu:7467652575122d8d54e767a68f141598bd855383@sha256:8781bc7e431b754c142edbfc937905fdf343db91f3fe19bbf54c362828db9849
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    # apt-get -qq update \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
