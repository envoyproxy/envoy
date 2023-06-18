FROM node:20.3-bullseye-slim@sha256:873d0db3312a942fd77d99117d2dbfc7e38c8cf51ab3a2157aa98ec5e9197ad8 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
