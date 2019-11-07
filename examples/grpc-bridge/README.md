# gRPC HTTP/1.1 to HTTP/2 bridge

This is an example of a key-value store where a client CLI, written in Python, updates a remote store, written in Go, using the stubs generated for both languages. More info at [envoy docs](https://www.envoyproxy.io/docs/envoy/latest/start/sandboxes/grpc_bridge).
 
Running clients that uses gRPC Stubs and sends messages through a proxy
that upgrades the HTTP requests from http/1.1 to http/2. This is a more detailed
implementation of the envoy documentation at https://www.envoyproxy.io/docs/envoy/latest/start/sandboxes/grpc_bridge

* Client: talks in python and sends HTTP/1.1 requests (gRPC stubs)
  * Client-Proxy: Envoy setup that acts as an egress and converts the HTTP/1.1 call to HTTP/2.
* Server: talks in golang and receives HTTP/2 requests (gRPC stubs)
  * Server-Proxy: Envoy setup that acts as an ingress and receives the HTTP/2 calls

`[client](http/1.1) -> [client-egress-proxy](http/2) -> [server-ingress-proxy](http/2) -> [server]`

# Running in 3 Steps

* Generate Stubs: both the `client` and `server` stubs in `python` and `go` respectively to be used by each server.
* Start Both Client and Server Servers and Proxies: `
* Use the Client CLI to make calls to the kv server.

## Generate Stubs

* Uses the `protos` dir and generates the stubs for both `client` and `server`
* Inspect the file `docker-compose-protos.yaml` with the gRPC protoc commands to generate the stubs.

```console
$ docker-compose -f docker-compose-protos.yaml up --remove-orphans
Starting grpc-bridge_stubs_python_1 ... done
Starting grpc-bridge_stubs_go_1     ... done
Attaching to grpc-bridge_stubs_go_1, grpc-bridge_stubs_python_1
grpc-bridge_stubs_go_1 exited with code 0
grpc-bridge_stubs_python_1 exited with code 0
```

* The files created were the `kv` modules for both the client and server respective dir.
  * Note that both stubs are their respective languages.
  * For each language, use its ways to include the stubs as an external module.

```console
$ ls -la client/kv/kv_pb2.py
-rw-r--r--  1 mdesales  CORP\Domain Users  9527 Nov  6 21:59 client/kv/kv_pb2.py

$ ls -la server/kv/kv.pb.go
-rw-r--r--  1 mdesales  CORP\Domain Users  9994 Nov  6 21:59 server/kv/kv.pb.go
```
