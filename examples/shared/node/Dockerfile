FROM node:20.5-bullseye-slim@sha256:336eb176d37030cc09157bd37a2fb45a0a841e425232c71483d9cd6b37c3ee9e as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
