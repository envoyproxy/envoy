FROM envoyproxy/envoy-build-ubuntu:b0ff77ae3f25b0bf595f9b8bba46b489723ab446

ENV DEBIAN_FRONTEND=noninteractive

RUN curl -sL https://packages.cloud.google.com/apt/doc/apt-key.gpg \
    | apt-key --keyring /usr/share/keyrings/cloud.google.gpg add - \
    && curl -fsSL https://apt.kitware.com/keys/kitware-archive-latest.asc \
    | apt-key add - \
    && apt-get update \
    && apt-get install --no-install-recommends -y -qq gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
