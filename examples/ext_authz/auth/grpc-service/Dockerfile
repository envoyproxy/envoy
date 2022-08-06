FROM golang:alpine@sha256:f8e128fa8aa891fe29e22e6401686dffef9bd4c3f5b552b09a7c29f7379979c1 AS builder

RUN apk --no-cache add make
COPY . /app
RUN make -C /app/grpc-service

FROM alpine@sha256:7580ece7963bfa863801466c0a488f11c86f85d9988051a9f9c68cb27f6b7872

COPY --from=builder /app/grpc-service/server /app/server
CMD ["/app/server", "-users", "/etc/users.json"]
