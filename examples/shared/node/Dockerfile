FROM node:20.6-bullseye-slim@sha256:36d587c8542eafc209a8419b1b8ba3225b002b6d15e75bd094c78ca6cd605ec3 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
