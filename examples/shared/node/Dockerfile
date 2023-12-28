FROM node:21.4-bookworm-slim@sha256:3a3b69c7013ac1233d4570f14108572e3f6dac3e2cefa8ef63be2885f702d033 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
