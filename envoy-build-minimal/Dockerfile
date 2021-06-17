FROM ubuntu:bionic

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y -qq --no-install-recommends \
            build-essential \
            ca-certificates \
            clang-format \
            clang-tidy \
            cmake \
            curl \
            git \
            jq \
            locales \
            lsb-release \
            ninja-build \
            python3.8 \
            python3.8-distutils \
            python3.8-venv \
            sudo \
            time \
            wget \
            xz-utils \
    && apt-get install -y -qq aspell \
    && cd "/tmp" \
    && wget https://github.com/bazelbuild/bazelisk/releases/download/v1.7.4/bazelisk-linux-amd64 \
    && chmod +x bazelisk-linux-amd64 \
    && cp -a bazelisk-linux-amd64 /usr/bin/bazel \
    && bazel --version \
    && ls -alh /root/.cache/bazelisk \
    && cd /tmp || exit 1 \
    && echo "c37d4f51e26ec8ab96b03d84af8c050548d7288a47f755ffb57706c6c458e027  /tmp/shellcheck-v0.7.0/shellcheck" > sc.checksum \
    && wget -qO- https://github.com/koalaman/shellcheck/releases/download/v0.7.0/shellcheck-v0.7.0.linux.x86_64.tar.xz | tar -xJf - \
    && sha256sum -c sc.checksum \
    && sudo cp shellcheck-v0.7.0/shellcheck /usr/local/bin \
    && rm -rf shellcheck-v0.7.0/ sc.checksum \
    && cd || exit 1 \
    && update-alternatives --install /usr/bin/python python /usr/bin/python3.8 1 \
    && update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.8 1 \
    && mkdir -p /src/workspace \
    && groupadd -r pcap \
    && apt-get autoremove -y \
    && apt-get clean \
    && rm -rf /tmp/* /var/tmp/* \
    && rm -rf /var/lib/apt/lists/*
