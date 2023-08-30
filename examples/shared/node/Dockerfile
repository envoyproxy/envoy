FROM node:20.5-bullseye-slim@sha256:f54a16be368537403c6f20e6e9cfa400f4b71c71ae9e1e93558b33a08f109db6 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
