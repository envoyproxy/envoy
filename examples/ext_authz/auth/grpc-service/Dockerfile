FROM golang:alpine@sha256:d84b1ff3eeb9404e0a7dda7fdc6914cbe657102420529beec62ccb3ef3d143eb AS builder

RUN apk --no-cache add make
COPY . /app
RUN make -C /app/grpc-service

FROM alpine@sha256:7580ece7963bfa863801466c0a488f11c86f85d9988051a9f9c68cb27f6b7872

COPY --from=builder /app/grpc-service/server /app/server
CMD ["/app/server", "-users", "/etc/users.json"]
