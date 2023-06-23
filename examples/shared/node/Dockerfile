FROM node:20.3-bullseye-slim@sha256:c92280d8fb6e7ca07f258c45e9f18cb643ea798a5441855a05e982cfd2b90789 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
