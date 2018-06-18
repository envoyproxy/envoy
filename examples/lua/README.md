In this example, we show how a Lua filter can be used with the Envoy
proxy. The Envoy proxy [configuration](./envoy.yaml) includes a lua
filter that contains two functions namely
`envoy_on_request(request_handle)` and
`envoy_on_response(response_handle)` as documented
[here](https://www.envoyproxy.io/docs/envoy/latest/configuration/http_filters/lua_filter).



# Usage
1. `docker-compose build`
2. `docker-compose up`
3. `curl -v localhost:8000`

## Sample Output:

Curl output should include our headers:

```
# <b> curl -v localhost:8000</b>
* Rebuilt URL to: localhost:8000/
*   Trying 127.0.0.1...
* Connected to localhost (127.0.0.1) port 8000 (#0)
> GET / HTTP/1.1
> Host: localhost:8000
> User-Agent: curl/7.47.0
> Accept: */*
>
< HTTP/1.1 200 OK
< x-powered-by: Express
< content-type: application/json; charset=utf-8
< content-length: 544
< etag: W/"220-PQ/ZOdrX2lwANTIy144XG4sc/sw"
< date: Thu, 31 May 2018 15:29:56 GMT
< x-envoy-upstream-service-time: 2
< response-body-size: 544            <-- This is added to the response header by our Lua script. --<
< server: envoy
<
{
  "path": "/",
  "headers": {
    "host": "localhost:8000",
    "user-agent": "curl/7.47.0",
    "accept": "*/*",
    "x-forwarded-proto": "http",
    "x-request-id": "0adbf0d3-8dfd-452f-a80a-1d6aa2ab06e2",
    "foo": "bar",                    <-- This is added to the request header by our Lua script. --<
    "x-envoy-expected-rq-timeout-ms": "15000",
    "content-length": "0"
  },
  "method": "GET",
  "body": "",
  "fresh": false,
  "hostname": "localhost",
  "ip": "::ffff:172.18.0.2",
  "ips": [],
  "protocol": "http",
  "query": {},
  "subdomains": [],
  "xhr": false,
  "os": {
    "hostname": "5ad758105577"
  }
* Connection #0 to host localhost left intact
}
```