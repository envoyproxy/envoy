FROM node:20.8-bullseye-slim@sha256:64ba042504e23ad45a5ed02c9c66aa9e8af22617e3a430f715535106760971f8 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
