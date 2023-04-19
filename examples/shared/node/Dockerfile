FROM node:19.9-bullseye-slim@sha256:602626a95fde52072639a61a3bb6dad86aea5d6776687788f707d6bc2c6e0b87 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
