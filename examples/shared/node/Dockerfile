FROM node:21.5-bookworm-slim@sha256:9df21707e27d98ee83e079fa47c5c0c5fba6c768e566643f8944b00b00afc4a2 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH


FROM node-base as yarn
ARG SERVICE_PORT=3000
ENV DEBIAN_FRONTEND=noninteractive \
    SERVICE_PORT=$SERVICE_PORT
COPY --chmod=755 ./scripts/entrypoint.sh /entrypoint.sh
COPY --chmod=755 ./scripts/build.sh /usr/local/bin/build.sh
COPY --chmod=755 ./scripts/dev.sh /usr/local/bin/dev.sh
COPY --chmod=644 ./routes.jq /usr/local/share/routes.jq
RUN apt-get update \
    && apt-get -qq install -y --no-install-recommends gosu jq netcat-traditional yq
ENTRYPOINT ["/entrypoint.sh"]
CMD ["dev.sh"]
HEALTHCHECK \
    --interval=2s \
    --timeout=1s \
    --start-period=1s \
    --retries=30 \
    CMD nc -zv localhost "$SERVICE_PORT"


FROM yarn as yarn-routed
COPY --chmod=755 ./scripts/build-routed.sh /usr/local/bin/build.sh
