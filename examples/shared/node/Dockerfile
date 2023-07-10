FROM node:20.3-bullseye-slim@sha256:57ae74ffd7253c71b6e896ae585184d26446ba10e689a02921a1852d24d82d74 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
