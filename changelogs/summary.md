**Summary of changes**:

* Envoy Mobile can now be built without C++ exceptions using the `--define=envoy_exceptions=disabled` Bazel flag.
* Add the logical `OR` operation to value matchers.
* Add xDS support for Envoy Mobile Android (AAR) library.
* Add configurable HTTP status when a global rate limit service fails.
* Opentelemetry tracer: add support for environment resource detector.
* Added HTTP basic auth extension.
* Add support for ext_authz to send route metadata.
* Allow per route body buffering configuration in ext_authz.
* Datadog: honor extracted sampling decisions to avoid dropping samples.
* gRPC side streams: make idle connection timeout configurable.
* Support CEL expressions in ext_proc for extraction of request or response atributes.
* HTTP: clear hop by hop `Transfer-Encoding` header.
* Redis: Add support for the `WATCH` and `GETDEL` commands.
* Adds strict mode for stateful session filter, that rejects requests if destination host is not available.
* Internal redirects: support passing headers from response to request.
* Add implementation of the `drop_overload` Cluster API.
* HTTP/2: discard the `Host` header when `:authority` is present.
* grpc_http1_bridge: add `<ignore_query_params>` option.
* Access Log: Add `EMIT_TIME` command operator.
* ECDS now supports composite filter.
* Enable new oghttp2 codec for HTTP/2 connections.
