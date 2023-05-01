FROM node:20.0-bullseye-slim@sha256:2cd3fdd49eb8bc1b942be824030469beca57fb6723edaaa9041e593b79a56cd1 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
