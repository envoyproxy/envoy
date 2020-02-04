Version history
---------------

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
