FROM node:20.7-bullseye-slim@sha256:737d756b6f93734c6d4732576c6a82d5bc7c47f2c1643d6877736387a5455429 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
