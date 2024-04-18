FROM alpine:3.16@sha256:e4cdb7d47b06ba0a062ad2a97a7d154967c8f83934594d9f2bd3efa89292996b

RUN mkdir /opt/app
WORKDIR /opt/app

# Note: WORKDIR must already be set (as it is above) before installing npm.
# If WORKDIR is not set, then npm is installed at the container root,
# which then causes `npm install` to fail later.
RUN apk update && apk add nodejs npm
RUN npm install dd-trace

COPY ./http.js /opt/app/http.js

CMD ["node", "--require", "dd-trace/init", "http.js"]
