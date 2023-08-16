FROM node:20.5-bullseye-slim@sha256:a2fe8fd3975d4b378abf721519df7cf0d465b8b8fb29e6bbe4d4a79c871bcded as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
