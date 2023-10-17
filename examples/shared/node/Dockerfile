FROM node:20.8-bullseye-slim@sha256:d9730e4dd0f0ca135d2407592646252880089cd9ea2405f54da9c076e3fd8ce7 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
