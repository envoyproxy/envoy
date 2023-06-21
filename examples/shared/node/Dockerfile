FROM node:20.3-bullseye-slim@sha256:4c4d1930c335191ebcf049eec6a4d35571b1fb9468ab0b8a403724c1a6d23f58 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
