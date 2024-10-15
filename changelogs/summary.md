**Summary of changes**:

* Envoy now logs warnings when `internal_address_config` is not set.  If you see this logged warning and wish to retain trusted status for internal addresses you must explicitly configure `internal_address_config` (which will turn off the warning) before the next Envoy release.
* Removed support for (long deprecated) opentracing. 
* Added a configuration setting for the maximum size of response headers in responses.
* Added support for `connection_pool_per_downstream_connection` flag in tcp connection pool.
* For the strict DNS and logical DNS cluster types, the `dns_jitter` field allows spreading out DNS refresh requests
* Added dynamic metadata matcher support `dynamic metadata input` and `dynamic metadata input matcher`.
* The xff original IP detection method now supports using a list of trusted CIDRs when parsing `x-forwarded-for`.
* QUIC server and client support certificate compression, which can in some cases reduce the number of round trips required to setup a connection.
* Added the ability to monitor CPU utilization in Linux based systems via `cpu utilization monitor` in overload manager.
* Added new access log command operators (`%START_TIME_LOCAL%` and `%EMIT_TIME_LOCAL%`) formatters (`%UPSTREAM_CLUSTER_RAW%` `%DOWNSTREAM_PEER_CHAIN_FINGERPRINTS_256%`, and `%DOWNSTREAM_PEER_CHAIN_SERIALS%`) as well as significant boosts to json parsing.  See release notes for details
* Added support for `%BYTES_RECEIVED%`, `%BYTES_SENT%`, `%UPSTREAM_HEADER_BYTES_SENT%`, `%UPSTREAM_HEADER_BYTES_RECEIVED%`, `%UPSTREAM_WIRE_BYTES_SENT%`, `%UPSTREAM_WIRE_BYTES_RECEIVED%` and access log substitution strings for UDP tunneling flows.
* Added ECDS support for UDP session filters.
