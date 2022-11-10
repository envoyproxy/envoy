# envoy-go-filter-samples

Samples for implementing filters by using pure Go, based on the low level APIs from [envoy-go-extension](https://github.com/mosn/envoy-go-extension).

For high level usage, please wait for the API in [MOSN](https://github.com/mosn/mosn), coming soon.

## Samples

1. [simple](https://github.com/mosn/envoy-go-filter-samples/blob/master/simple/filter.go), basic usage demo.

## Build

Compile Go filter into libgolang.so.

```shell
make build
```

## Run

Notice: only works on amd64 architecture CPU yet.

```shell
docker run -d -p 10000:10000 \
    -v `pwd`/libgolang.so:/usr/local/envoy-go-extension/libgolang.so \
    -v `pwd`/envoy.yaml:/etc/envoy/envoy-golang.yaml \
    mosnio/envoy-go-extension:latest
```

```shell
# 1. it will proxy to httpbin.org/header
# we will see the request header and response header setted by Go.

$ curl 'http://127.0.0.1:10000/headers' -v
*   Trying 127.0.0.1:10000...
* Connected to 127.0.0.1 (127.0.0.1) port 10000 (#0)
> GET /headers HTTP/1.1
> Host: 127.0.0.1:10000
> User-Agent: curl/7.79.1
> Accept: */*
>
* Mark bundle as not supporting multiuse
< HTTP/1.1 200 OK
< date: Sun, 30 Oct 2022 02:02:16 GMT
< content-type: application/json
< content-length: 260
< server: envoy
< access-control-allow-origin: *
< access-control-allow-credentials: true
< x-envoy-upstream-service-time: 1014
< rsp-header-from-go: bar-test
<
{
  "headers": {
    "Accept": "*/*",
    "Host": "httpbin.org",
    "Req-Header-From-Go": "foo-test",
    "User-Agent": "curl/7.79.1",
    "X-Amzn-Trace-Id": "Root=1-635ddb28-7c27d0446b0281c10c3170d8",
    "X-Envoy-Expected-Rq-Timeout-Ms": "15000"
  }
}

# 2. local reply from Go
$ curl 'http://127.0.0.1:10000/localreply'
forbidden from go, path: /localreply
```
