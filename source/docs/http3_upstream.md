### Overview

Support for Upstream HTTP/3 connections is slightly more complex than for HTTP/1 or HTTP/2
and requires specific configurations to enable it. If HTTP/3 is configured in auto_config,
HTTP/3 connections will only be attempted to servers which
advertise HTTP/3 support either via [HTTP Alternative Services](https://tools.ietf.org/html/rfc7838)
or the [HTTPS DNS resource record](https://datatracker.ietf.org/doc/html/draft-ietf-dnsop-svcb-https-04).
If no such advertisement exists, then HTTP/2 or HTTP/1 will be used instead. Further,
HTTP/3 runs over QUIC (which uses UDP) and not over TCP (which HTTP/1 and HTTP/2 use).
It is not uncommon for network devices to block UDP traffic, and hence block HTTP/3. This
means that upstream HTTP/3 connection attempts might be blocked by the network and should fall
back to using HTTP/2. On such networks, the upstream connection code needs to
track that HTTP/3 connects attempts are not succeeding and avoid making connections
which are doomed to fail. On networks where HTTP/3 is working correctly, however, the
upstream connection code should avoid attempting HTTP/2 unnecessarily.

### Components

#### Broken HTTP/3 tracking
The `BrokenHttp3Tracker` is a per-grid object which tracks the state of HTTP/3 for
that grid. When HTTP/3 is marked as broken, it remains broken for 5 minutes after
which point it is no longer broken. If it is marked as broken again, it remains
broken for twice the previous period, up to a maximum of 1 day. If it is instead
marked as confirmed, then the period is reset to the default and the next time
HTTP/3 is marked as broken it will be for the initial 5 minutes.

#### Http Server Protocols Cache
The `HttpServerPropertiesCache` is responsible for tracking servers which advertise HTTP/3.
These advertisements may arrive via HTTP Alternative Service (alt-svc) or soon via the HTTPS
DNS RR. Currently only advertisements which specify the same hostname and port are stored.

#### Connectivity Grid
The `ConnectivityGrid` is a `ConnectionPool` which wraps a `Http3ConnPoolImpl` connection pool
(QUIC) and a `HttpConnPoolImplMixed` connection pool (TCP). If HTTP/3 is currently broken, stream
requests will be sent directly to the mixed connection pool. If HTTP/3 is not advertised in the
alternate protocols cache, stream requests will be sent directly to the mixed connection pool.
If, however, HTTP/3 is advertised and working then the request will be sent to the HTTP/3
connection pool. After 300ms, if this request has not succeeded, then the request will also be
sent to the mixed pool, and whichever succeeds first will be passed back up to the caller.

### Required configuration

Specific Envoy configuration is required in order to enable the previously described components.

#### Alternate Protocols Cache Filter

The Alternate Protocols Cache Filter must be enabled in order for alt-svc headers to be parsed
and stored in the cache, and the `alternate_protocols_cache_options` field must be specified
in the filter config.

#### Auto Cluster Pool

The "Auto" must be enabled via the Upstream `HttpProtocolOptions` message. The
`upstream_protocol_options` field needs to specify `auto_config`. This config muse specify
all three protocols: HTTP/1, HTTP/2 and HTTP/3.

#### Alternate Protocols Cache Options

In addition, the `alternate_protocols_cache_options` field must be specified in
`upstream_protocol_options` and the value must match that in the filter config.
