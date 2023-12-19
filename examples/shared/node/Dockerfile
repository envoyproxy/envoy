FROM node:21.4-bookworm-slim@sha256:6233b0e9257da6a7df6aed5d726625735b70f0ef1f77b23fc462b8020d1056b9 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
