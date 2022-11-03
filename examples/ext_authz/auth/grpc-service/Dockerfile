FROM golang:alpine@sha256:e4dcdac3ed37d8c2b3b8bcef2909573b2ad9c2ab53ba53c608909e8b89ccee36 AS builder

RUN apk --no-cache add make
COPY . /app
RUN make -C /app/grpc-service

FROM alpine@sha256:bc41182d7ef5ffc53a40b044e725193bc10142a1243f395ee852a8d9730fc2ad

COPY --from=builder /app/grpc-service/server /app/server
CMD ["/app/server", "-users", "/etc/users.json"]
