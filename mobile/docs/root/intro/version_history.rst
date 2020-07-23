Version history
---------------

0.3.1 (July 23, 2020)
=====================

In the last few months the team has continued to harden Envoy Mobile with production exposure.

Stability and Production Hardening:

- Improves concurrency management for retries (:issue:`#774 <774>`, :issue:`#811 <811>`)
- Adds complete coverage for c++ code (:issue:`#791 <791>`, :issue:`#792 <792>`)
- Updates platform interfaces as production experience informs ergonomics (:issue:`#798 <798>`, :issue:`#802 <802>`, :issue:`#808 <808>`)
- Updates termination signal handling (:issue:`#835 <835>`)
- Updates battery and cpu analysis (:issue:`#852 <852>`)
- Adds bi-directional compression support (:issue:`#861 <861>`)
- Fixes SIGPIPE handling for iOS (:issue:`#965 <965>`)
- Introduces formal style for cross-platform enums (:issue:`#966 <966>`)
- Updates to build to C++17 (:issue:`#964 <#964>`)

Observability:

- Adds emission rule for upstream_rq_active (:issue:`#775 <775>`)
- Adds the ability to observe number of retries that happened on a particular stream (:issue:`#821 <821>`, :issue:`#820 <820>`, :issue:`#813 <813>`)
- Adds Http::Dispatcher stats (:issue:`#871 <871>`)
- Adds stats for 4xx codes (:issue:`#902 <902>`)

Extensibility:

- Introduces platform filter interfaces and bridging (:issue:`#795 <795>`, :issue:`#840 <840>`, :issue:`#858 <858>`, :issue:`#913 <913>`, :issue:`#940 <940>`, :issue:`#955 <955>`, :issue:`#943 <943>`, :issue:`#962 <962>`)
- Introduces Envoy's extension platform (:issue:`#860 <860>`)

Lastly, and perhaps most importantly, we have adopted a formal `inclusive language policy <https://github.com/lyft/envoy-mobile/blob/main/CONTRIBUTING.md#inclusive-language-policy>`_
(:issue:`#948 <948>`) and updated all necessary locations (:issue:`#944 <944>`, :issue:`#945 <945>`, :issue:`#946 <946>`)

0.3.0 (Mar 26, 2020)
====================

This is the first release of Envoy Mobile Lyft is using in a production application! 🎉

Since early November, when the team tagged v0.2.0, we have been hard at work to stabilize the library,
and harden it via experiments with Lyft's Alpha and Beta releases. We have released Lyft's production
binaries with Envoy Mobile for a couple weeks now, and are starting to expose a percentage of our
production clients to Envoy Mobile with this release.

Since v0.2.3 we have largely focused on observability:

- Adds improved logging (:issue:`#701 <701>`, :issue:`#702 <702>`, :issue:`#722 <722>`)
- Adds basic stats for retries :issue:`#718 <718>`)
- Adds ``x-envoy-attempt-count`` response header (:issue:`#751 <751>`)
- Adds visibility over `virtual clusters <https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#config-route-v3-virtualcluster>`_ (:issue:`#768 <768>`, :issue:`#771 <771>`)

Additional changes:

- Fixes trailers missing on iOS (:issue:`#703 <703>`)
- Adds ability to set DNS failure refresh rate (:issue:`#714 <714>`)
- Adds docs on the EnvoyClientBuilder (:issue:`#745 <745>`)

0.2.3 (Feb 21, 2020)
====================

This release provides stabilization fixes as follow-up changes to 0.2.2:

- Fixes race that caused double-deletion of HCM active streams crashing (:issue:`#669 <669>`)
- Fixes DNS resolution when starting Envoy Mobile offline on iOS (:issue:`#672 <672>`)
- Fixes for API listener crashes (:issue:`#667 <667>` and :issue:`#674 <674>`)
- Fixes for linking and assertions (:issue:`#663 <663>`)
- Fixes bad access in ~DnsCache() in Envoy upstream (:issue:`#690 <690>`)
- Fixes bug in Dynamic Forward Proxy Cluster in Envoy Upstream (:issue:`#678 <678>`)
- Adds known issue assertion that prevents crash on force-close (:issue:`#699 <699>`)

Additional changes:

- Allows zero for upstream timeout specification (:issue:`#659 <659>`)
- Adds process logging for Android (:issue:`#684 <684>`)
- Adds the ability to decide upstream protocol for requests (:issue:`#697 <697>`)


0.2.2 (Feb 3, 2020)
===================

Envoy Mobile v0.2.2 changes how network requests are performed to no longer use Envoy's `AsyncClient` and to instead consume the `ApiListener` directly (:issue:`#616 <616>`).

Additional changes:

- Domain specification when starting the library is no longer supported (:issue:`#641 <641>`, :issue:`#642 <642>`). Envoy Mobile now uses the authority specified when starting a new stream
- Less aggressive retry back-off policies (:issue:`#652 <652>`)

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
