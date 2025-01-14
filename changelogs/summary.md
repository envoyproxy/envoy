**Summary of changes**:

* c-ares:
  - [CVE-2024-25629](https://github.com/c-ares/c-ares/security/advisories/GHSA-mg26-v6qh-x48q) Out of bounds read in c-ares (DNS)
* HTTP:
  - RFC1918 addresses are no longer considered to be internal addresses by default. This addresses a security issue for Envoys in multi-tenant mesh environments.
  - Shadow requests are now streamed in parallel with the original request.
  - Local replies now traverse the filter chain if 1xx headers have been sent to the client.
* Tracing:
  - Removed support for (long deprecated) Opencensus tracing extension.
* Wasm:
  - The route cache will *not* be cleared by default if a Wasm extension modifies the request headers and the ABI version of wasm extension is larger than 0.2.1.
  - Remove previously deprecated xDS attributes from `get_property`, use `xds` attributes instead.
  - Added Wasm VM reload support and support for plugins writtin in Go.
* Access log:
  - New implementation of the JSON formatter is enabled by default.
* CSRF:
  - Increase the statistics counter `missing_source_origin` only for requests with a missing source origin.
* DNS:
  - Added nameserver rotation and query timeouts/retries to the c-ares resolver.
* Formatter:
  - `NaN` and `Infinity` values of float will be serialized to `null` and `inf` respectively in the metadata (`DYNAMIC_METADATA`, `CLUSTER_METADATA`, etc.) formatters.
* OAuth2:
  - `use_refresh_token` is now enabled by default.
  - Implement the Signed Double-Submit Cookie pattern.
* QUIC:
  - Enable UDP GRO in QUIC client connections by default.
* SDS:
  - Relaxed the backing cluster validation for Secret Discovery Service (SDS).
* TLS:
  - Added support for P-384 and P-521 curves for server certificates, improved upstream SNI and SAN validation support.
