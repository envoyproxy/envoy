To learn about this sandbox and for instructions on how to run it please head over
to the [envoy docs](https://www.envoyproxy.io/docs/envoy/latest/start/sandboxes/grpc_bridge)

# gRPC HTTP/1.1 to HTTP/2 bridge

This is an example of a key-value store where a client CLI, written in Python, updates a remote store, written in Go, using the stubs generated for both languages. 
 
Running clients that uses gRPC stubs and sends messages through a proxy
that upgrades the HTTP requests from http/1.1 to http/2. This is a more detailed
implementation of the Envoy documentation at https://www.envoyproxy.io/docs/envoy/latest/start/sandboxes/grpc_bridge

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

## Start Both Client and Server and Proxies

* After the stubs are in place, start the containers described in `docker-compose.yaml`.

```console
$ docker-compose up --build
```

* Inspect the files `client/envoy-proxy.yaml` and `server/envoy-proxy.yaml`, as they define configs for their respective container, comparing port numbers and other specific settings.

Notice that you will be interacting with the client container, which hosts
the client python CLI. The port numbers for the proxies and the containers are displayed
by the `docker-compose ps`, so it's easier to compare with the `\*/envoy-proxy.yaml` config files for each
of the containers how they match.

Note that the client container to use is `grpc-bridge_grpc-client_1` and binds to no port
as it will use the `python` CLI.

```console
$ docker-compose ps
             Name                            Command               State                             Ports
------------------------------------------------------------------------------------------------------------------------------------
grpc-bridge_grpc-client-proxy_1   /docker-entrypoint.sh /usr ... Up      10000/tcp, 0.0.0.0:9911->9911/tcp, 0.0.0.0:9991->9991/tcp
grpc-bridge_grpc-client_1         /bin/sh -c tail -f /dev/null     Up
grpc-bridge_grpc-server-proxy_1   /docker-entrypoint.sh /usr ... Up      10000/tcp, 0.0.0.0:8811->8811/tcp, 0.0.0.0:8881->8881/tcp
grpc-bridge_grpc-server_1         /bin/sh -c /bin/server           Up      0.0.0.0:8081->8081/tcp
```

## Use the Client CLI

* Since the containers are running, you can use the client container to interact with the gRPC server through the proxies
* The client has the methods `set key value` and `get key` to use the in-memory key-value store.

```console
$ docker-compose exec grpc-client /client/grpc-kv-client.py set foo bar
setf foo to bar
```

> NOTE: You could also run docker instead of docker-compose `docker exec -ti grpc-bridge_grpc-client_1 /client/grpc-kv-client.py set foo bar`

* The server will display the gRPC call received by the server, and then the access logs from the proxy for the SET method.
  * Note that the proxy is propagating the headers of the request

```console
grpc-server_1        | 2019/11/07 16:33:58 set: foo = bar
grpc-server-proxy_1  | [2019-11-07T16:33:58.856Z] "POST /kv.KV/Set HTTP/1.1" 200 - 15 7 3 1 "172.24.0.3" "python-requests/2.22.0" "c11cf735-0647-4e67-965c-5b1e362a5532" "grpc" "172.24.0.2:8081"
grpc-client-proxy_1  | [2019-11-07T16:33:58.855Z] "POST /kv.KV/Set HTTP/1.1" 200 - 15 7 5 3 "172.24.0.3" "python-requests/2.22.0" "c11cf735-0647-4e67-965c-5b1e362a5532" "grpc" "172.24.0.5:8811"
```

* Getting the value is no different

```console
$ docker-compose exec grpc-client /client/grpc-kv-client.py get foo
bar
```

> NOTE: You could also run docker instead of docker-compose `docker exec -ti grpc-bridge_grpc-client_1 /client/grpc-kv-client.py get foo`

* The logs in the server will show the same for the GET method.
  * Note that again the request ID is proxied through

```console
grpc-server_1        | 2019/11/07 16:34:50 get: foo
grpc-server-proxy_1  | [2019-11-07T16:34:50.456Z] "POST /kv.KV/Get HTTP/1.1" 200 - 10 10 2 1 "172.24.0.3" "python-requests/2.22.0" "727d4dcd-a276-4bb2-b4cc-494ae7119c24" "grpc" "172.24.0.2:8081"
grpc-client-proxy_1  | [2019-11-07T16:34:50.455Z] "POST /kv.KV/Get HTTP/1.1" 200 - 10 10 3 2 "172.24.0.3" "python-requests/2.22.0" "727d4dcd-a276-4bb2-b4cc-494ae7119c24" "grpc" "172.24.0.5:8811"
```

# Troubleshooting

* Errors building the `client` or `server` are related to the missing gRPC stubs.
* Make sure to produce the stubs before building
  * The error below is when the server is missing the stubs in the kv dir.

```console
$ go build -o server
go: finding github.com/envoyproxy/envoy/examples/grpc-bridge latest
go: finding github.com/envoyproxy/envoy/examples latest
go: finding github.com/envoyproxy/envoy/examples/grpc-bridge/server/kv latest
go: finding github.com/envoyproxy/envoy/examples/grpc-bridge/server latest
build github.com/envoyproxy/envoy: cannot load github.com/envoyproxy/envoy/examples/grpc-bridge/server/kv: no matching versions for query "latest"
```
