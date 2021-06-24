FROM ubuntu:bionic

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y -qq --no-install-recommends \
            build-essential \
            ca-certificates \
            cmake \
            curl \
            git \
            gnupg-agent \
            gnupg2 \
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
    # aspell installed without --no-install-recommends
    && apt-get install -y -qq aspell \
    # google.api py
    && apt-get install -y -qq --no-install-recommends --no-install-suggests python3-pip \
    && python3.8 -m pip install googleapis-common-protos \
    && apt-get remove --purge -y -qq python3-pip \
    # clang-tidy, clang-format
    && echo "deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-11 main" >> /etc/apt/sources.list \
    && wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add - \
    && apt-get update \
    && apt-get install -y -qq --no-install-recommends clang-format-11 clang-tidy-11 \
    && cd /tmp || exit 1 \
    # buildozer
    && wget https://github.com/bazelbuild/buildtools/releases/download/2.2.1/buildozer \
    && cp -a buildozer /usr/local/bin \
    && chmod +x /usr/local/bin/buildozer \
    # buildifier
    && wget https://github.com/bazelbuild/buildtools/releases/download/2.2.1/buildifier \
    && cp -a buildifier /usr/local/bin \
    && chmod +x /usr/local/bin/buildifier \
    # bazelisk
    && wget https://github.com/bazelbuild/bazelisk/releases/download/v1.7.4/bazelisk-linux-amd64 \
    && chmod +x bazelisk-linux-amd64 \
    && cp -a bazelisk-linux-amd64 /usr/bin/bazel \
    && bazel --version \
    # shellcheck
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
    && apt-get autoremove --purge -y \
    && apt-get clean \
    && rm -rf /tmp/* /var/tmp/* \
    && rm -rf /var/lib/apt/lists/*
