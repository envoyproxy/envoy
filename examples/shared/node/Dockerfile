FROM node:20.5-bullseye-slim@sha256:45dbc40906607e48873d26caa1a968f1a5187dd5e78e0e47205eea15393fd9c3 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
