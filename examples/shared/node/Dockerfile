FROM node:21.2-bullseye-slim@sha256:3623ec53085a55d84b620ef5365eb9c8e3f95bd21782b64387520092068bd9ab as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
