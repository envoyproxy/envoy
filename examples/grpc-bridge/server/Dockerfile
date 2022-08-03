FROM golang:1.18.5-bullseye@sha256:24a25853ae62f360fcb869da7229f37237044b9c7c689368a5fbbb556bfcf652 as builder

WORKDIR /build

# Resolve and build Go dependencies as Docker cache
COPY go.mod /build/go.mod
COPY go.sum /build/go.sum
COPY kv/go.mod /build/kv/go.mod

ENV GO111MODULE=on
RUN go mod download

COPY service.go /build/main.go
COPY kv/ /build/kv

# Build for linux
ENV GOOS=linux
ENV GOARCH=amd64
ENV CGO_ENABLED=0
RUN go build -o server

# Build the main container (Linux Runtime)
FROM debian:bullseye-slim@sha256:a811e62769a642241b168ac34f615fb02da863307a14c4432cea8e5a0f9782b8
WORKDIR /root/

# Copy the linux amd64 binary
COPY --from=builder /build/server /bin/

ENTRYPOINT /bin/server
