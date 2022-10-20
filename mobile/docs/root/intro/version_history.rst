Version history
---------------

Pending Release
===============

Breaking changes:

- ios/android: remove ``addH2RawDomains`` method. (:issue: `#2590 <2590>`)
- build: building on macOS now requires Xcode 14.0. (:issue:`#2544 <2544>`)

Bugfixes:

- android: fix engine startup crash for when admin interface is enabled. (:issue:`#2520 <2520>`)
- android: respect system security policy when determining whether clear text requests are allowed. (:issue:`#2528 <2528>`)

Features:

- kotlin/c++: add option to support platform provided certificates validation interfaces on Android. (:issue `#2144 <2144>`)
- api: Add a ``setPerTryIdleTimeoutSeconds()`` method to C++ EngineBuilder.
- kotlin: add a way to tell Envoy Mobile to respect system proxy settings by calling an ``enableProxying(true)`` method on the engine builder. (:issue:`#2416 <2416>`)
- kotlin: add a ``enableSkipDNSLookupForProxiedRequests(true)`` knob for controlling whether Envoy waits on DNS response in the dynamic forward proxy filter for proxied requests. (:issue:`#2602 <2602>`)
- api: Add various methods to C++ EngineBuilder to bring it to parity with the Java and Obj-C builders. (:issue:`#2498 <2498>`)
- api: Add support for String Accessors to the C++ EngineBuilder. (:issue:`#2498 <2498>`)
- api: added upstream protocol to final stream intel. (:issue:`#2613 <2613>`)

0.5.0 (September 2, 2022)
===========================

Breaking changes:

- api: replace the ``drainConnections()`` method with a broader ``resetConnectivityState()``. (:issue:`#2225 <2225>`).
- api: disallow setting 'host' header directly (:issue:`#2275 <2275>`)
- android: respect Android's NetworkSecurityPolicy isCleartextTrafficPermitted APIs.
- net: enable happy eyeballs by default (:issue:`#2272 <2272>`)
- iOS: remove support for installing via CocoaPods, which had not worked since 2020 (:issue:`#2215 <2215>`)
- iOS: enable usage of ``NWPathMonitor`` by default (:issue:`#2329 <2329>`)
- iOS: replace ``enableNetworkPathMonitor`` with a new ``setNetworkMonitoringMode`` API to allow disabling monitoring (:issue:`#2345 <2345>`)
- iOS: release artifacts no longer embed bitcode
- api: engines are no longer a singleton, you may need to update your code to only create engines once and hold on to them.
  You also cannot assume that an `envoy_engine_t` value of `1` will return the default engine.
  Support for using multiple engines concurrently is coming later. (:issue:`#2129 <2129>`)

Bugfixes:

- iOS: change release artifacts to use xcframeworks (:issue:`#2216 <2216>`)
- Cronvoy: Cancel the scheduled onSendWindowAvailable callback when a stream is cancelled (:issue:`#2213 <2213>`)
- fix bug where writing prevented the read loop from running (:issue:`#2221 <2221>`)
- Android: update Kotlin standard libraries to 1.6.21 (:issue:`#2256 <2256>`)
- fix bug where finalStreamIntel was not consistently set on cancel (:issue:`#2285 <2285>`)
- iOS: fix termination crash in ProvisionalDispatcher (:issue:`#2059 <2059>`)
- api: make headers lookup in ``HeadersBuilder`` and ``Headers`` case-insensitive. Rename ``allHeaders`` method to ``caseSensitiveHeaders``. (:issue:`#2383 <2383>`, :issue:`#2400 <2400>``)
- iOS: use correct DNS resolver when using C++ config builder (:issue: `#2378 <2378 >`)

Features:

- Android, iOS, & C++: add support for registering a platform KV store (:issue: `#2134 <2134>`, :issue: `#2335 <2335>`, :issue: `#2430 <2430>`)
- api: add option to extend the keepalive timeout when any frame is received on the owning HTTP/2 connection. (:issue:`#2229 <2229>`)
- api: add option to control whether Envoy should drain connections after a soft DNS refresh completes. (:issue:`#2225 <2225>`, :issue:`#2242 <2242>`)
- api: add option to disable the gzip decompressor. (:issue: `#2321 <2321>`) (:issue: `#2349 <2349>`)
- api: add option to enable the brotli decompressor. (:issue `#2342 <2342>`) (:issue: `#2349 <2349>`)
- api: add option to enable socket tagging. (:issue `#1512 <1521>`)
- configuration: enable h2 ping by default. (:issue: `#2270 <2270>`)
- android: enable the filtering of unroutable families by default. (:issues: `#2267 <2267>`)
- instrumentation: add timers and warnings to platform-provided callbacks (:issue: `#2300 <2300>`)
- iOS: add support for integrating Envoy Mobile via the Swift Package Manager
- android: create simple persistent SharedPreferencesStore (:issue: `#2319 <2319>`)
- iOS: A documentation archive is now included in the GitHub release artifact (:issue: `#2335 <2335>`)
- api: improved C++ APIs compatibility with Java / Kotlin / Swift (:issue `#2362 <2362>`)
- Android: default to use a ``getaddrinfo``-based system DNS resolver instead of c-ares (:issue: `#2419 <2419>`)
- iOS: add ``KeyValueStore`` protocol conformance to ``UserDefaults`` (:issue: `#2452 <2452>`)
- iOS: add experimental option to force all connections to use IPv6. (:issue: `#2396 <2396>`)
- android: force the use of IPv6 addresses for all connections. (:issue: `#2510 <2510>`)

0.4.6 (April 26, 2022)
========================

Breaking changes:

- iOS: the minimum supported iOS version is now 12.0 (:issue:`#2084 <2084>`)

Bugfixes:

- happy eyeballs: fix missing runtime configuration  (:issue:`#2068 <2068>`)
- iOS: fix CocoaPods releases (:issue:`#2175 <2175>`)
- android: fix Maven releases (:issue:`#2183 <2183>`)
- dns: prevent dns refresh if network is unchanged (:issue:`#2122 <2122>`)
- happy eyeballs: fix crash on Android (:issue:`#2132 <2132>`)
- ios: fix termination crash in ProvisionalDispatcher (:issue:`#2059 <2059>`)

Features:

- api: added Envoy's response flags to final stream intel (:issue:`#2009 <2009>`)
- size: the size of the dynamic library was reduced by ~46% (:issue:`#2053 <2053>`)
- tls: updated the bundled root certificates (:issue:`#2016 <2016>`)
- api: expose "received byte count" in the Java API (:issue:`#2004 <2004>`)
- bazel: allow configuring Android toolchain versions (:issue:`#2041 <2041>`)
- ios: add explicit flow control onSendWindowAvailable to public interface (:issue:`#2046 <2046>`)
- api: add option to add a list of H2-Raw domain names (:issue:`#2088 <2088>`)
- ios: add support for toggling trust chain verification (:issue:`#2104 <2104>`)
- api: add support for configuring minimum DNS refresh rate and per-host max connections (:issue:`#2123 <2123>`)
- h3/quic: add experimental option to the Android/JVM EngineBuilder (:issue:`#2163 <2163>`)
- android: include debug info in release binary (:issue:`#2188 <2188>`)

0.4.5 (January 13, 2022)
========================

Based off Envoy `v1.21.0 <https://github.com/envoyproxy/envoy/releases/tag/v1.21.0>`_

Bugfixes:

- Decompressor: decompress even when `no-transform` is specified  (:issue:`#1995 <1995>`)

Features:

- HTTP: any negotiated ALPN now passed up as `x-envoy-upstream-alpn` header (:issue: `#1965 <1965>`)


0.4.4 (December 30, 2021)
=========================

Bugfixes:

- Explicit Flow Control: fix a reset-after-fin bug with explicit flow control (:issue:`#1898 <1898>`)
- HTTP: solve a race condition when resumeData is too early (:issue:`#1926 <1926>`)
- HTTP: fix race condition for last resumeData (:issue:`#1936 <1936>`)
- HTTP: expand response buffer limit to 1Mb (:issue:`#1987 <1987>`)
- JNI: fix support for non-direct byte buffers (:issue:`#1950 <1950>`)
- Network: make SrcAddrSocketOptionImpl safely handle null addresses (:issue:`#1905 <1905>`)
- Obj-c: fix NSString to envoy_data conversion (:issue:`#1958 <1958>`)
- Observability: fix V6 interface binding logging (:issue:`#1959 <1959>`)

Features:

- Cronvoy: use Explicit Flow Control (:issue:`#1924 <1924>`)
- DNS: add ability to use fallback nameservers. Android only (:issue:`#1953 <1953>`)
- DNS: add EngineBuilder API to filter unroutable families (:issue:`#1984 <1984>`)
- Interface Binding: support interface binding on Android (:issue:`#1897 <1897>`)
- Interface Binding: filter alt interfaces for binding by well-known prefixes (:issue:`#1901 <1901>`)
- Network: use NWPathMonitor to determine network reachability on iOS (:issue:`#1874 <1874>`)
- Networl: add iOS/Android support for enabling Happy Eyeballs (:issue:`#1971 <1971>`)
- Observability: instrument first active interfaces when switching socket modes (:issue:`#1889 <1889>`)

0.4.3 (October 20, 2021)
========================

Bugfixes:

- Headers: delete splitting comma-separated header values and add specific logic to the RetryPolicy classes (:issue:`#1752 <1752>`)
- Headers: prevent nil header value crashes in obj-c (:issue:`#1826 <1826>`)

Features:

- Android: conditionally build internal getifaddrs support (:issue:`#1772 <1772>`)
- Connection handling: add API to drain connections (:issue:`#1729 <1729>`)
- Connection handling: remove alternate clusters (:issue:`#1756 <1756>`)
- DNS: use v4_preferred option (:issue:`#1811 <1811>`)
- DNS: EngineBuilder API addDnsQueryTimeoutSeconds (:issue:`#1583 <1583>`)
- HTTP: advertise h2 alpn string when forcing h2 (:issue:`#1737 <1737>`)
- HTTP: integrate callback-based error path (:issue:`#1592 <1592>`)
- HTTP: add H2 ping config API (:issue:`#1770 <1770>`)
- HTTP: per try idle timeout (:issue:`#1805 <1805>`)
- HTTP: Switching to Envoy Mobile HCM (:issue:`#1716 <1716>`)
- Interface Binding: allow to be configured in programmatic API (:issue:`#1832 <1832>`)
- Interface Binding: support conditionally binding active alt interface (:issue:`#1834 <1834>`)
- Interface Binding: implement initial heuristic for binding alternate interface (:issue:`#1858 <1858>`)
- Network: introduce singleton configurator (:issue:`#1816 <1816>`)
- Observability: emit events based on ENVOY_LOG_EVENT (:issue:`#1746 <1746>`)
- Observability: add engine API to dump stats (:issue:`#1733 <1733>`)
- Observability: emit envoy event every time envoy bug macro is called (:issue:`#1771 <1771>`)
- Observability: add method for enabling admin interface (:issue:`#1636 <1636>`)
- Observability: expose StreamIntel on stream callbacks (:issue:`#1657 <1657>`)
- Observability: emit events for assertions (:issue:`#1703 <1703>`)

0.4.2 (July 27, 2021)
=====================

Bugfixes:

- Filters: Prevent spurious cancellation callbacks from the gRPC error path (:issue:`#1560 <1560>`)
- JNI: null terminate strings before passing to NewStringUTF (:issue:`#1589 <1589>`)

Features:

- Cronvoy: explicit flow control mode (:issue:`#1513 <1513>`)
- Debugging: add Scope Trackers for ease of debugging (:issue:`#1498 <1498>`)
- DNS: prefetch DNS hostnames (:issue:`#1535 <1535>`)
- Exception Handling: convert Envoy Exceptions to crashes (:issue:`#1505 <1505>`)
- Stats: expose flushStats on the Engine (:issue:`#1486 <1486>`)

0.4.1 (May 28, 2021)
====================

Bugfixes:

- Fixes platform-bridged filters crash when resumed asynchronously after stream termination.
- Disables route timeout by default.

Features:

- Connection classes will open minimum of 2 under most circumstances to a given endpoint and distribute requests between them (previously, only 1).
- Adds Pulse support for stats tags.
- Enables configuration of stream idle timeout.
- Introduces a Python interface compatible with the popular Requests library.
- Adds experimental QUIC integration test.
- Adds pure JVM support.


0.4.0 (March 23, 2021)
======================

This is a large release. Moving forward the team will aim to release smaller version updates.
The following is a very high-level overview of the larger changes going into this release.

Richer Platform-level Feature Set:

- Adds pluggable logging capabilities via :ref:`setLogger <api_starting_envoy>`
- Adds :ref:`platform APIs <api_stats>` for emitting time-series data
- Adds platform Filters
- Adds API for accessing arbitrary strings from platform runtime via :ref:`addStringAccessor <api_starting_envoy>`

Additional Language Bindings:

- Alpha version of python APIs via C++ bindings
- Alpha version exposing cronet compatible APIs

Continued Bug fixes uncovered by additional testing:

- Fixes several memory management corner-cases
- Fixes several issues that have led to production crashes

Additional hardening of the codebase via extensive testing:

- Adds end-to-end testing that covers roundtrip code execution from the platform layer to the core layer.
- Adds coverage CI runs for core C++ core

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

Lastly, and perhaps most importantly, we have adopted a formal `inclusive language policy <https://github.com/envoyproxy/envoy-mobile/blob/main/CONTRIBUTING.md#inclusive-language-policy>`_
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
