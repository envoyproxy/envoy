FROM golang:1.20.2-bullseye@sha256:51ff22f03320894402290ba7dfd83ee05b61e58b5381d76b40f2e3a370d81da3 as builder

COPY . /app
RUN make -C /app/grpc-service

FROM debian:bullseye-slim

COPY --from=builder /app/grpc-service/server /app/server
CMD ["/app/server", "-users", "/etc/users.json"]
