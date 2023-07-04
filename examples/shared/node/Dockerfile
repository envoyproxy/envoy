FROM node:20.3-bullseye-slim@sha256:256d40234a2840aa05c918adfa2b051030580feb33ba7c80001fa37ca4883ee1 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
