Version history
---------------

0.3.0 (Mar 26, 2020)
====================

This is the first release of Envoy Mobile Lyft is using in a production application! 🎉

Since early November, when the team tagged v0.2.0, we have been hard at work to stabilize the library,
and harden it via experiments with Lyft's Alpha and Beta releases. We have released Lyft's production
binaries with Envoy Mobile for a couple weeks now, and are starting to expose a percentage of our
production clients to Envoy Mobile with this release.

Since v0.2.3 we have largely focused on observability:

- Adds improved logging (#701, #702, #722)
- Adds basic stats for retries #718)
- Adds ``x-envoy-attempt-count`` response header (#751)
- Adds visibility over `virtual clusters <https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#config-route-v3-virtualcluster>`_ (#768, #771)

Additional changes:

- Fixes trailers missing on iOS (#703)
- Adds ability to set DNS failure refresh rate (#714)
- Adds docs on the EnvoyClientBuilder (#745)

0.2.3 (Feb 21, 2020)
====================

This release provides stabilization fixes as follow-up changes to 0.2.2:

- Fixes race that caused double-deletion of HCM active streams crashing (#669)
- Fixes DNS resolution when starting Envoy Mobile offline on iOS (#672)
- Fixes for API listener crashes (#667 and #674)
- Fixes for linking and assertions (#663)
- Fixes bad access in ~DnsCache() in Envoy upstream (#690)
- Fixes bug in Dynamic Forward Proxy Cluster in Envoy Upstream (#678)
- Adds known issue assertion that prevents crash on force-close (#699)

Additional changes:

- Allows zero for upstream timeout specification (#659)
- Adds process logging for Android (#684)
- Adds the ability to decide upstream protocol for requests (#697)


0.2.2 (Feb 3, 2020)
===================

Envoy Mobile v0.2.2 changes how network requests are performed to no longer use Envoy's `AsyncClient` and to instead consume the `ApiListener` directly (#616).

Additional changes:

- Domain specification when starting the library is no longer supported (#641, #642). Envoy Mobile now uses the authority specified when starting a new stream
- Less aggressive retry back-off policies (#652)

0.2.1 (Jan 6, 2020)
===================

This release of Envoy Mobile contains some small improvements:

- Maven release script for Android builds
- Streams are now limited to a single "terminal" callback
- Keepalive settings are now in place to better support connection switching and long-lived streams
- Properly support IPv6 networks by using updated DNS settings

0.2.0 (Nov 4, 2019)
===================

Envoy Mobile v0.2 is a fundamental shift in how mobile clients use Envoy. Envoy Mobile now provides native Swift/Kotlin APIs that call through to Envoy directly (rather than using Envoy as a proxy), which apps use to create and interact with network streams.

This release includes a variety of new functionality:
- HTTP request and streaming support
- gRPC streaming support through a built-in codec
- Automatic retries using Envoy's retry policies
- Programmatic, typed configuration for launching the Envoy network library

0.1.1 (Sep 11, 2019)
====================

This release is identical to v0.1.0, but packages the license and support for additional architectures.

0.1.0 (Jun 18, 2019)
====================

Initial open source release.
