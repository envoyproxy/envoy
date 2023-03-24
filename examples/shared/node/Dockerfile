FROM node:19.8-bullseye-slim@sha256:f5eb5c04eeb0bdaff44e179b84b57b8958f2fc6fb814641d3a5f3fe27d93e248 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
