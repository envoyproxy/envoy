FROM golang:1.16 as build
WORKDIR /go/src/github.com/envoyproxy/envoy/examples/http-protocol-options/http3-client/
COPY *.go go.mod go.sum ./
RUN CGO_ENABLED=0 GOOS=linux go build -a -installsuffix cgo -o h3client .

FROM scratch
WORKDIR /root/
COPY --from=build /go/src/github.com/envoyproxy/envoy/examples/http-protocol-options/http3-client/h3client ./
CMD ["/root/h3client"]
