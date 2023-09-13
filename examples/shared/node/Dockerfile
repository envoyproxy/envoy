FROM node:20.6-bullseye-slim@sha256:3e1a690b3dda477e2a0191acc7c545a2758fea90181bd9c87475096b76956351 as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
