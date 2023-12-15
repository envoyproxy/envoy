FROM node:21.4-bullseye-slim@sha256:3afb93c631e692edb893a403d25f9b4cae4b03748244cc9804c31dd957235682 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
