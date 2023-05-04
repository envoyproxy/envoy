FROM node:20.0-bullseye-slim@sha256:702d475af4b8045f701afd10ea865f4454e9aab4fe3e92d50e8f36906055f456 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
