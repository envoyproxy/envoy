FROM gcr.io/envoy-ci/envoy-build@sha256:f0b2453c3587e3297f5caf5e97fbf57c97592c96136209ec13fe2795aae2c896

ARG USERNAME=vscode
ARG USER_UID=501
ARG USER_GID=$USER_UID

ENV BUILD_DIR=/build
ENV TEST_TMPDIR=/build/tmp
ENV ENVOY_STDLIB=libstdc++

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
  && apt-get -y install --no-install-recommends libpython2.7 sudo net-tools psmisc 2>&1 \
  #
  # Create a non-root user to use if preferred - see https://aka.ms/vscode-remote/containers/non-root-user.
  && groupadd --gid $USER_GID $USERNAME \
  && useradd -s /bin/bash --uid $USER_UID --gid $USER_GID -m $USERNAME \
  # [Optional] Add sudo support for non-root user
  && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
  && chmod 0440 /etc/sudoers.d/$USERNAME \
  #
  # Clean up
  && apt-get autoremove -y \
  && apt-get clean -y \
  && rm -rf /var/lib/apt/lists/* \

ENV DEBIAN_FRONTEND=