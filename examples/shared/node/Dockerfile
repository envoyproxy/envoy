FROM node:20.8-bullseye-slim@sha256:682c1557c5a8cd6f8a78db3bd315ed968b3a854de2a16c2b8ce713cc92152062 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
