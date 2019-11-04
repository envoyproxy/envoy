Version history
---------------

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
