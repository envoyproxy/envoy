FROM golang:1.20.1-bullseye@sha256:63ed3bc6b428713c2bb1980e534d70e8ab82a4484ec044183df9caca6e9d82cb as builder

COPY . /app
RUN make -C /app/grpc-service

FROM debian:bullseye-slim

COPY --from=builder /app/grpc-service/server /app/server
CMD ["/app/server", "-users", "/etc/users.json"]
