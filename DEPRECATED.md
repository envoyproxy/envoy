# DEPRECATED

As of release 1.3.0, Envoy will follow a
[Breaking Change Policy](https://github.com/envoyproxy/envoy/blob/master//CONTRIBUTING.md#breaking-change-policy).

The following features have been DEPRECATED and will be removed in the specified release cycle.
A logged warning is expected for each deprecated item that is in deprecation window.

## Version 1.8.0 (pending)

* Use of the v1 API is deprecated. See envoy-announce
  [email](https://groups.google.com/forum/#!topic/envoy-announce/oPnYMZw8H4U).
* Use of the legacy 
  [ratelimit.proto](https://github.com/envoyproxy/envoy/blob/b0a518d064c8255e0e20557a8f909b6ff457558f/source/common/ratelimit/ratelimit.proto)
  is deprecated, in favor of the proto defined in
  [date-plane-api](https://github.com/envoyproxy/envoy/blob/master/api/envoy/service/ratelimit/v2/rls.proto)
  Prior to 1.8.0, Envoy can use either proto to send client requests to a ratelimit server with the use of the
  `use_data_plane_proto` boolean flag in the [ratelimit configuration](https://github.com/envoyproxy/envoy/blob/master/api/envoy/config/ratelimit/v2/rls.proto).
  However, when using the deprecated client a warning is logged.
* Use of the --v2-config-only flag.
* Use of both `use_websocket` and `websocket_config` in
  [route.proto](https://github.com/envoyproxy/envoy/blob/master/api/envoy/api/v2/route/route.proto)
  is deprecated. Please use the new `upgrade_configs` in the
  [HttpConnectionManager](https://github.com/envoyproxy/envoy/blob/master/api/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.proto)
  instead.

## Version 1.7.0

* Admin mutations should be sent as POSTs rather than GETs. HTTP GETs will result in an error
  status code and will not have their intended effect. Prior to 1.7, GETs can be used for
  admin mutations, but a warning is logged.
* Rate limit service configuration via the `cluster_name` field is deprecated. Use `grpc_service`
  instead.
* gRPC service configuration via the `cluster_names` field in `ApiConfigSource` is deprecated. Use
  `grpc_services` instead. Prior to 1.7, a warning is logged.
* Redis health checker configuration via the `redis_health_check` field in `HealthCheck` is
  deprecated. Use `custom_health_check` with name `envoy.health_checkers.redis` instead. Prior
  to 1.7, `redis_health_check` can be used, but warning is logged.
* `SAN` is replaced by `URI` in the `x-forwarded-client-cert` header.
* The `endpoint` field in the http health check filter is deprecated in favor of the `headers`
  field where one can specify HeaderMatch objects to match on.
* The `sni_domains` field in the filter chain match was deprecated/renamed to `server_names`.

## Version 1.6.0 (March 20, 2018)

* DOWNSTREAM_ADDRESS log formatter is deprecated. Use DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT
  instead.
* CLIENT_IP header formatter is deprecated. Use DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT instead.
* 'use_original_dst' field in the v2 LDS API is deprecated. Use listener filters and filter chain
  matching instead.
* `value` and `regex` fields in the `HeaderMatcher` message is deprecated. Use the `exact_match`
  or `regex_match` oneof instead.

## Version 1.5.0 (Dec 4, 2017)

* The outlier detection `ejections_total` stats counter has been deprecated and not replaced. Monitor
  the individual `ejections_detected_*` counters for the detectors of interest, or
  `ejections_enforced_total` for the total number of ejections that actually occurred.
* The outlier detection `ejections_consecutive_5xx` stats counter has been deprecated in favour of
  `ejections_detected_consecutive_5xx` and `ejections_enforced_consecutive_5xx`.
* The outlier detection `ejections_success_rate` stats counter has been deprecated in favour of
  `ejections_detected_success_rate` and `ejections_enforced_success_rate`.

## Version 1.4.0 (Aug 24, 2017)

* Config option `statsd_local_udp_port` has been deprecated and has been replaced with
  `statsd_udp_ip_address`.
* `HttpFilterConfigFactory` filter API has been deprecated in favor of `NamedHttpFilterConfigFactory`.
* Config option `http_codec_options` has been deprecated and has been replaced with `http2_settings`.
* The following log macros have been deprecated: `log_trace`, `log_debug`, `conn_log`,
  `conn_log_info`, `conn_log_debug`, `conn_log_trace`, `stream_log`, `stream_log_info`,
  `stream_log_debug`, `stream_log_trace`. For replacements, please see
  [logger.h](https://github.com/envoyproxy/envoy/blob/master/source/common/common/logger.h).
* The connectionId() and ssl() callbacks of StreamFilterCallbacks have been deprecated and
  replaced with a more general connection() callback, which, when not returning a nullptr, can be
  used to get the connection id and SSL connection from the returned Connection object pointer.
* The protobuf stub gRPC support via `Grpc::RpcChannelImpl` is now replaced with `Grpc::AsyncClientImpl`.
  This no longer uses `protoc` generated stubs but instead utilizes C++ template generation of the
  RPC stubs. `Grpc::AsyncClientImpl` supports streaming, in addition to the previous unary, RPCs.
* The direction of network and HTTP filters in the configuration will be ignored from 1.4.0 and
  later removed from the configuration in the v2 APIs. Filter direction is now implied at the C++ type
  level. The `type()` methods on the `NamedNetworkFilterConfigFactory` and
  `NamedHttpFilterConfigFactory` interfaces have been removed to reflect this.
