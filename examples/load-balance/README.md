# Envoy WebAssembly Filter

In this example, we show how a WebAssembly(WASM) filter can be used with the Envoy
proxy. The Envoy proxy [configuration](./envoy.yaml) includes a Webassembly filter
as documented [here](https://www.envoyproxy.io/docs/envoy/latest/).
<!--TODO(bianpengyuan): change to the url of Wasm filter once the doc is ready.-->



## Quick Start

1. `docker-compose build`
2. `docker-compose up`
3. `curl -v localhost:18000`

Curl output should include our headers:

```
# <b> curl -v localhost:8000</b>
* Rebuilt URL to: localhost:18000/
*   Trying 127.0.0.1...
* TCP_NODELAY set
* Connected to localhost (127.0.0.1) port 18000 (#0)
> GET / HTTP/1.1
> Host: localhost:18000
> User-Agent: curl/7.58.0
> Accept: */*
> 
< HTTP/1.1 200 OK
< content-length: 13
< content-type: text/plain
< location: envoy-wasm
< date: Tue, 09 Jul 2019 00:47:14 GMT
< server: envoy
< x-envoy-upstream-service-time: 0
< newheader: newheadervalue
< 
example body
* Connection #0 to host localhost left intact
```

## Build WASM Module

Now you want to make changes to the C++ filter ([envoy_filter_http_wasm_example.cc](envoy_filter_http_wasm_example.cc))
and build the WASM module ([envoy_filter_http_wasm_example.wasm](envoy_filter_http_wasm_example.wasm)).

1. Build WASM module
   ```shell
   bazel build //examples/wasm:envoy_filter_http_wasm_example.wasm
   ```

## Build the Envoy WASM Image

<!--TODO(incfly): remove this once we upstream WASM to envoyproxy main repo.-->

For Envoy WASM runtime developers, if you want to make changes, please

1. Follow [instructions](https://github.com/envoyproxy/envoy-wasm/blob/master/WASM.md).
2. Modify `docker-compose.yaml` to mount your own Envoy.
