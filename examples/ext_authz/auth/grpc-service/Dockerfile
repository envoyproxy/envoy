FROM golang:alpine@sha256:af22f4a8328063faee4b28da1b1bbccccb6f3ccaa0a07006f9d3aa2da43d18c2 AS builder

RUN apk --no-cache add make
COPY . /app
RUN make -C /app/grpc-service

FROM alpine@sha256:7580ece7963bfa863801466c0a488f11c86f85d9988051a9f9c68cb27f6b7872

COPY --from=builder /app/grpc-service/server /app/server
CMD ["/app/server", "-users", "/etc/users.json"]
