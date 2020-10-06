FROM gcr.io/envoy-ci/envoy-build:b480535e8423b5fd7c102fd30c92f4785519e33a

ARG USERNAME=vscode
ARG USER_UID=501
ARG USER_GID=$USER_UID

ENV BUILD_DIR=/build
ENV ENVOY_STDLIB=libstdc++

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get -y update \
  && apt-get -y install --no-install-recommends libpython2.7 net-tools psmisc vim 2>&1 \
  # Create a non-root user to use if preferred - see https://aka.ms/vscode-remote/containers/non-root-user.
  && groupadd --gid $USER_GID $USERNAME \
  && useradd -s /bin/bash --uid $USER_UID --gid $USER_GID -m $USERNAME -G pcap -d /build \
  # [Optional] Add sudo support for non-root user
  && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
  && chmod 0440 /etc/sudoers.d/$USERNAME

ENV DEBIAN_FRONTEND=
ENV PATH=/opt/llvm/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

ENV CLANG_FORMAT=/opt/llvm/bin/clang-format
