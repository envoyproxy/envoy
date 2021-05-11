### Overview

Support for Upstream HTTP/3 connections is slightly more complex than for HTTP/1 or HTTP/2
and this document attempts to describe it.

### Components

#### Broken HTTP/3 tracking
The `BrokenHttp3Tracker` is a per-grid object which tracks the state of HTTP/3 for
that grid. When HTTP/3 is marked as broken, it remains broken for 5 minutes after
which point it is no longer broken. If it is marked as broken again, it remains
broken for twice the previous period, up to a maximum of 1 day. If it is instead
marked as confirmed, then the period is reset to the default and the next time
HTTP/3 is marked as broken it will be for the initial 5 minutes.

#### Alternate Protocols Cache
The `AlternateProtocolsCache` is responsible for tracking servers which advertise HTTP/3.
These advertisements may arrive via HTTP Altnernate Service (alt-svc) or soon via the HTTPS
DNS RR. Currently only advertisements which specify the same hostname and port are stored.

#### Connectivity Grid
The `ConnectivityGrid` is a `ConnectionPool` which wraps a `Http3ConnPoolImpl` connection pool
(QUIC) and a `HttpConnPoolImplMixed` connection pool (TCP). If HTTP/3 is currently broken, stream
requets will be sent directly to the mixed connection pool. If HTTP/3 is not advertised in the
alternate protocols cache, stream requets will be sent directly to the mixed connection pool.
If, however, HTTP/3 is advertised and working then the request will be sent to the HTTP/3
connection pool. After 300ms, if this request has not succeeded, then the request will also be
sent to the mixed pool, and whichever succeeds first will be passed back up to the caller.

### Required configuration

Specific Envoy configuration is required in order to enable the previous. components

#### Auto Cluster Pool

The "Auto" must be enabled via the Upstream `HttpProtocolOptions` message. The
`upstream_protocol_options` field needs to specify `auto_config`. This config muse specify
all three protocols: HTTP/1, HTTP/2 and HTTP/3.

#### Alternate Protocols Cache Options

In addition, the `alternate_protocols_cache_options` field must be specified.
