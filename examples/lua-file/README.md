In this example, we show how to use a lua file in an Envoy Lua filter.
 The Envoy proxy [configuration](./envoy.yaml) includes a lua
filter that contains two functions namely
`envoy_on_request(request_handle)` as documented
[here](https://www.envoyproxy.io/docs/envoy/latest/configuration/http_filters/lua_filter).
This function calls an imported [lua file](./foo.lua) that we copied into the container on build.



# Usage
1. Build and run docker containers: `docker-compose up --build`
2. Make an unauthenticated request: `curl -i localhost:8000`
3. Make an authenticated request: `curl -H "Authorization: some_token_here" -i http://localhost:8000`


## Sample output:
Unauthenticated requests will cause the Envoy lua filter to return a 401.

```
curl -i http://localhost:8000
HTTP/1.1 401 Unauthorized
content-length: 35
date: Wed, 19 Dec 2018 21:54:12 GMT
server: envoy

unable to find authorization header%
```

Authenticated requests will be sent to the upstream.

Curl output should include our headers:

```
 curl -H "Authorization: some_token_here" -i http://localhost:8000
HTTP/1.1 200 OK
x-powered-by: Express
content-type: application/json; charset=utf-8
content-length: 575
etag: W/"23f-zgCflq0NjKcZAsvn6r8wMS0/Bws"
date: Wed, 19 Dec 2018 21:55:37 GMT
x-envoy-upstream-service-time: 34
server: envoy

{
  "path": "/",
  "headers": {  <------------ Notice that the app did not receive the Authorization header the lua filter removed.
    "host": "localhost:8000",
    "user-agent": "curl/7.54.0",
    "accept": "*/*",
    "x-forwarded-proto": "http",
    "x-request-id": "e9173261-0f9c-4e0c-a66a-85196a272c98",
    "lua-filter-header-example": "lua_was_here", <------------ This was added by our lua filter.
    "x-envoy-expected-rq-timeout-ms": "15000",
    "content-length": "0"
  },
  "method": "GET",
  "body": "",
  "fresh": false,
  "hostname": "localhost",
  "ip": "::ffff:172.18.0.3",
  "ips": [],
  "protocol": "http",
  "query": {},
  "subdomains": [],
  "xhr": false,
  "os": {
    "hostname": "a37aca653010"
  }
}%
```