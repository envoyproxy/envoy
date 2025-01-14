**Summary of changes**:

[CVE-2024-25629](https://github.com/c-ares/c-ares/security/advisories/GHSA-mg26-v6qh-x48q): Out of bounds read in c-ares (DNS)

* RFC1918 addresses are no longer considered to be internal addresses by default. This addresses a security issue for Envoy's in multi-tenant mesh environments.
* http: Shadow requests are now streamed in parallel with the original request.
* tracing: Removed support for (long deprecated) opencensus tracing extension.
* wasm: The route cache will *not* be cleared by default if a wasm extension modifies the request headers and the ABI version of wasm extension is larger than 0.2.1.
* wasm: Remove previously deprecated xDS attributes from `get_property`, use `xds` attributes instead.
* access_log: New implementation of the JSON formatter is enabled by default.
* csrf: Increase the statistics counter `missing_source_origin` only for requests with a missing source origin.
* dns: added nameserver rotation and query timeouts/retries to the c-ares resolver.
* formatter: NaN and Infinity values of float will be serialized to `null` and `"inf"` respectively in the metadata (`DYNAMIC_METADATA`, `CLUSTER_METADATA`, etc.) formatters.
* http: Local replies now traverse the filter chain if 1xx headers have been sent to the client.
* oauth2: `use_refresh_token` is now enabled by default.
* oauth2: Implement the Signed Double-Submit Cookie pattern.
* quic: Enable UDP GRO in QUIC client connections by default.
* sds: Relaxed the backing cluster validation for Secret Discovery Service (SDS).
* tls: added support for P-384 and P-521 curves for server certificates, improved upstream SNI and SAN validation support.
* wasm: added wasm VM reload support and support for plugins writtin in Go.
