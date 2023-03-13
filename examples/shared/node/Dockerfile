FROM node:19.7-bullseye-slim@sha256:424dd181b3be2a7aec23f6ba3b69e732745bad42e9b8a473efcb1399e76f12de as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
