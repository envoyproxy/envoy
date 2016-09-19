# envoy-grpc

This is an example usage of the Envoy [gRPC bridge filter](https://lyft.github.io/envoy/docs/configuration/http_filters/grpc_http1_bridge_filter.html#config-http-filters-grpc-bridge). Included is a gRPC in memory Key/Value store with a Python HTTP client. The Python client makes HTTP/1 requests through the Envoy sidecar process which are upgraded into HTTP/2 gRPC requests. Response trailers are then buffered and sent back to the client as a HTTP/1 header payload.

## Building the Go service

```bash
script/bootstrap
script/build
```

## Docker compose

To run the docker compose file, and set up both the Python and the gRPC containers
run:

```bash
docker-compose up --build
```

## Sending requests to the Key/Value store

```bash
# set a key
docker-compose exec python /client/client.py set foo bar
=> setf foo to bar

# get a key
docker-compose exec python /client/client.py get foo
=> bar
```
