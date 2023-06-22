FROM node:20.3-bullseye-slim@sha256:a70c22cb6ef7c6d809970b2889e5e556337fda8bfaa439b30c035efaef8fc3a1 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
