# DEPRECATED

As of release 1.3.0, Envoy will follow a
[Breaking Change Policy](https://github.com/lyft/envoy/blob/master//CONTRIBUTING.md#breaking-change-policy).

The following features have been DEPRECATED and will be removed in the specified release cycle.

* Version 1.4.0
  * Config option `statsd_local_udp_port` has been deprecated and has been replaced with
  `statsd_udp_ip_address`.
  * `HttpFilterConfigFactory` filter API has been deprecated in favor of `NamedHttpFilterConfigFactory`.
  * Config option `http_codec_options` has been deprecated and has been replaced with `http2_settings`.
