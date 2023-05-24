FROM node:20.2-bullseye-slim@sha256:95a950ec61796f4c00f6b208cb51000b8bd127ee53b0c1c52f2539a5ab66f8ef as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
