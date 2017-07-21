# DEPRECATED

As of release 1.3.0, Envoy will follow a
[Breaking Change Policy](https://github.com/lyft/envoy/blob/master//CONTRIBUTING.md#breaking-change-policy).

The following features have been DEPRECATED and will be removed in the specified release cycle.

* Version 1.4.0
  * Config option `statsd_local_udp_port` has been deprecated and has been replaced with
  `statsd_udp_ip_address`.
  * `HttpFilterConfigFactory` filter API has been deprecated in favor of `NamedHttpFilterConfigFactory`.
  * Config option `http_codec_options` has been deprecated and has been replaced with `http2_settings`.
  * The following log macros have been deprecated: `log_trace`, `log_debug`, `conn_log`,
  `conn_log_info`, `conn_log_debug`, `conn_log_trace`, `stream_log`, `stream_log_info`,
  `stream_log_debug`, `stream_log_trace`.  For replacements, please see
  [logger.h](https://github.com/lyft/envoy/blob/master/source/common/common/logger.h).

* Version 1.5.0
  * The connectionId() callback in StreamFilterCallbacks has been deprecated and has been replaced
  * with more general connection() callback, which, when not returning a nullptr, can be used to get
  * the connection id from the returned Connection object pointer.
