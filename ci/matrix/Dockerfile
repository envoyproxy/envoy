FROM ubuntu:25.10@sha256:7cc5e35f6567ee8c66d2abb4aab0fd866669e6207c237c3a8f0947a5c7f17092
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=tmpfs,target=/var/cache/apt \
    --mount=type=tmpfs,target=/var/lib/apt/lists \
    apt-get -qq update \
    && apt-get -qq upgrade -y \
    && apt-get -qq install --no-install-recommends -y \
        curl \
        git \
        gosu \
        libc6-dev \
        software-properties-common \
    && curl -fsSL -o /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64 \
    && chmod +x /usr/local/bin/bazel \
    && groupadd -g 1001 envoybuild \
    && useradd -m -u 1001 -g 1001 -s /bin/bash envoybuild
ARG MATRIX_SETUP=""
RUN --mount=type=tmpfs,target=/var/cache/apt \
    --mount=type=tmpfs,target=/var/lib/apt/lists \
    TMPFILE="$(mktemp)" \
    && echo "$MATRIX_SETUP" > "$TMPFILE" \
    && chmod +x "${TMPFILE}" \
    && "${TMPFILE}"
ARG MATRIX_SETUP_EXTRA=""
RUN --mount=type=tmpfs,target=/var/cache/apt \
    --mount=type=tmpfs,target=/var/lib/apt/lists \
    TMPFILE="$(mktemp)" \
    && echo "$MATRIX_SETUP_EXTRA" > "$TMPFILE" \
    && chmod +x "${TMPFILE}" \
    && "${TMPFILE}"
COPY --chmod=755 ci/matrix/entrypoint.sh /entrypoint.sh
COPY --chmod=755 ci/matrix/test.sh /usr/local/bin/test.sh
WORKDIR /workspace
ENTRYPOINT ["/entrypoint.sh"]
