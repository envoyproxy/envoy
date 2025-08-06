FROM gcr.io/envoy-ci/envoy-build:f4a881a1205e8e6db1a57162faf3df7aed88eae8@sha256:95d7afdea0f0f8881e88fa5e581db4f50907d0745ac8d90e00357ac1a316abe5

ARG USERNAME=vscode
ARG USER_UID=501
ARG USER_GID=$USER_UID

ENV BUILD_DIR=/build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get -y update \
  && apt-get -y install --no-install-recommends libpython2.7 net-tools psmisc vim python3-pip wget 2>&1 \
  # Install Go (use latest 1.22 to avoid toolchain issues)
  && GO_VERSION=1.22.8 \
  && wget -q "https://golang.org/dl/go${GO_VERSION}.linux-amd64.tar.gz" \
  && tar -C /usr/local -xzf "go${GO_VERSION}.linux-amd64.tar.gz" \
  && rm "go${GO_VERSION}.linux-amd64.tar.gz" \
  # Disable Go toolchain auto-download
  && echo 'export GOTOOLCHAIN=local' >> /etc/environment \
  # Create a non-root user to use if preferred - see https://aka.ms/vscode-remote/containers/non-root-user.
  && groupadd --gid $USER_GID $USERNAME \
  && useradd -s /bin/bash --uid $USER_UID --gid $USER_GID -m $USERNAME -G pcap -d /build \
  # [Optional] Add sudo support for non-root user
  && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
  && chmod 0440 /etc/sudoers.d/$USERNAME

ENV DEBIAN_FRONTEND=
ENV PATH=/usr/local/go/bin:/opt/llvm/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ENV GOTOOLCHAIN=local

ENV CLANG_FORMAT=/opt/llvm/bin/clang-format
