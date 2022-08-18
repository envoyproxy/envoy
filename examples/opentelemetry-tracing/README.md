## Envoy & OpenTelemetry

This is a basic example that runs three Envoy instances and the OpenTelemetry Collector.

It is very similar to the [Jaeger Tracing Example](https://www.envoyproxy.io/docs/envoy/latest/start/sandboxes/jaeger_tracing).

### Instructions

1. `docker-compose pull`
2. `docker-compose up --build -d`
3. Find the ID of the OpenTelemetry collector via `docker ps`
4. `docker logs <Collector ID>`
5. In another terminal window, `curl http://localhost:8000/trace/2`


At that point, you should see spans being exported from the Envoy instances,
including one from the `front-envoy`, one from `service1-inbound`, one from
`service1-outbound`, and one from `service2`.

You can also explore the Collector zpages via `http://localhost:55679/debug/tracez`.

Example span:

```
2022-08-18T00:37:23.946Z	info	ResourceSpans #0
Resource SchemaURL:
Resource labels:
     -> service.name: STRING(front-envoy)
ScopeSpans #0
ScopeSpans SchemaURL:
InstrumentationScope
Span #0
    Trace ID       : b64eccd10f938d188f60b9b9af592b8a
    Parent ID      :
    ID             : 523463cf4033c290
    Name           : egress localhost:8000
    Kind           : SPAN_KIND_CLIENT
    Start time     : 2022-08-18 00:37:23.261083 +0000 UTC
    End time       : 2022-08-18 00:37:23.288706 +0000 UTC
    Status code    : STATUS_CODE_UNSET
    Status message :
Attributes:
     -> node_id: STRING()
     -> zone: STRING()
     -> guid:x-request-id: STRING(64a47de5-efda-9c6f-a175-a9d1dde8e0de)
     -> http.url: STRING(http://localhost:8000/trace/2)
     -> http.method: STRING(GET)
     -> downstream_cluster: STRING(-)
     -> user_agent: STRING(Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/103.0.0.0 Safari/537.36)
     -> http.protocol: STRING(HTTP/1.1)
     -> peer.address: STRING(172.24.0.1)
     -> request_size: STRING(0)
     -> response_size: STRING(89)
     -> component: STRING(proxy)
     -> upstream_cluster: STRING(service1)
     -> upstream_cluster.name: STRING(service1)
     -> http.status_code: STRING(200)
     -> response_flags: STRING(-)
```
