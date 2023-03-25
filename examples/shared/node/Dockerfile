FROM node:19.8-bullseye-slim@sha256:462d3bd44fbb437a4287aae34fd9fc69832cca4311be0e27de6f2b5d877caceb as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
