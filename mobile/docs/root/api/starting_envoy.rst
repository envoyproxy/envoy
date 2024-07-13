.. _api_starting_envoy:

Starting Envoy
==============

----------------
``StreamClient``
----------------

Starting an instance of Envoy Mobile is done by building the ``Engine`` instance with ``EngineBuilder``. Requests are performed by the ``StreamClient`` provided by ``Engine``.

To obtain a ``StreamClient``, call ``streamClient()`` on the ``Engine`` instance (see below).

After the stream client is obtained, it should be stored and used to start network requests/streams.

**Kotlin example**::

  val streamClient = AndroidEngineBuilder(getApplication())
    .setLogLevel(LogLevel.WARN)
    ...
    .build()
    .streamClient()

**Swift example**::

  let streamClient = try EngineBuilder()
    .setLogLevel(.warn)
    ...
    .build()
    .streamClient()

-----------------
``EngineBuilder``
-----------------

This type is used to configure an instance of ``Engine`` before finally
creating the engine using ``.build()``.

Available builders are nearly all 1:1 between iOS/Android, and are documented below.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``addConnectTimeoutSeconds``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Specify the timeout for new network connections to hosts in Envoy Mobile clusters.

**Example**::

  // Kotlin
  builder.addConnectTimeoutSeconds(30L)

  // Swift
  builder.addConnectTimeoutSeconds(30)

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``addDNSFailureRefreshSeconds``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Specify the rate at which Envoy Mobile should refresh DNS in states of failure.

This should typically be a relatively aggressive range compared to the standard-state DNS refresh
rate, as it is required for Envoy Mobile to recover and continue making requests.

**Example**::

  // Kotlin
  builder.addDNSFailureRefreshSeconds(2, 5)

  // Swift
  builder.addDNSFailureRefreshSeconds(base: 2, max: 5)

~~~~~~~~~~~~~~~~~~~~~~~~
``addDNSRefreshSeconds``
~~~~~~~~~~~~~~~~~~~~~~~~

Specify the interval at which Envoy should forcefully refresh DNS.

**Example**::

  // Kotlin
  builder.addDNSRefreshSeconds(60L)

  // Swift
  builder.addDNSRefreshSeconds(60)

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``addDNSQueryTimeoutSeconds``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Specify the interval at which Envoy should timeout a DNS query.

**Example**::

  // Kotlin
  builder.addDNSQueryTimeoutSeconds(60L)

  // Swift
  builder.addDNSQueryTimeoutSeconds(60)

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``addDNSPreresolveHostnames``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add a list of hostnames to preresolve on Engine startup.

  // Kotlin
  builder.addDNSPreresolveHostnames(listOf("lyft.com", "google.com"))

  // Swift
  builder.addDNSPreresolveHostnames(["lyft.com", "google.com"])

~~~~~~~~~~~~~~~
``setLogLevel``
~~~~~~~~~~~~~~~

Specify the log level to be used when running the underlying Envoy engine.

**Example**::

  // Kotlin
  builder.setLogLevel(LogLevel.WARN)

  // Swift
  builder.setLogLevel(.warn)

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``addStreamIdleTimeoutSeconds``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Specifies the length of time a stream should wait without a headers or data event before timing out.
Defaults to 15 seconds.
See `the Envoy docs <https://www.envoyproxy.io/docs/envoy/latest/api-v3/extensions/filters/network/http_connection_manager/v3/http_connection_manager.proto#envoy-v3-api-field-extensions-filters-network-http-connection-manager-v3-httpconnectionmanager-stream-idle-timeout>`__
for further information.

**Example**::

  // Kotlin
  builder.addStreamIdleTimeoutSeconds(5L)

  // Swift
  builder.addStreamIdleTimeoutSeconds(5)

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``addPerTryIdleTimeoutSeconds``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Specifies the length of time a retry (including the initial attempt) should wait without a headers
or data event before timing out. Defaults to 15 seconds.
See `the Envoy docs <https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto.html#config-route-v3-retrypolicy>`__
for further information.

**Example**::

  // Kotlin
  builder.addPerTryIdleTimeoutSeconds(5L)

  // Swift
  builder.addPerTryIdleTimeoutSeconds(5)

~~~~~~~~~~~~~~~~~
``addAppVersion``
~~~~~~~~~~~~~~~~~

Specify the version of the app using Envoy Mobile.

**Example**::

  // Kotlin
  builder.addAppVersion("v1.2.3")

  // Swift
  builder.addAppVersion("v1.2.3")

~~~~~~~~~~~~
``addAppId``
~~~~~~~~~~~~

Specify the version of the app using Envoy Mobile.

**Example**::

  // Kotlin
  builder.addAppId("com.mydomain.myapp")

  // Swift
  builder.addAppId("com.mydomain.myapp)

~~~~~~~~~~~~~~~~~~~~~~~~~
``addNativeFilter``
~~~~~~~~~~~~~~~~~~~~~~~~~

Add a C++ filter to the Envoy Mobile filter chain

.. attention::

    This will only work if the C++ filter specified is linked into your Envoy Mobile build.
    For C++ and Android testing, calling addNativeFilter and linking the Envoy library by adding the
    library to ``envoy_build_config/extensions_build_config.bzl`` is sufficient.
    For iOS, due to enthusiastic garbage collection, and for upstream CI, to catch bugs, you will
    also need to forceRegister the filter in ``envoy_build_config/extension_registry.cc``
    Both platforms use proto syntax by default, but YAML is supported if you build with --define=envoy_yaml=enabled

**Example**::

  // Kotlin
  builder.addNativeFilter("envoy.filters.http.buffer", "[type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer] { max_request_bytes: { value: 5242880 } ")

  // Swift
  builder.addNativeFilter("envoy.filters.http.buffer", "[type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer] { max_request_bytes: { value: 5242880 } ")
~~~~~~~~~~~~~~~~~~~~~~
``setOnEngineRunning``
~~~~~~~~~~~~~~~~~~~~~~

Specify a closure to be called once Envoy's engine finishes its async startup and begins running.

When Envoy is instantiated, its initializer returns before all of its internal configuration
completes. This interface provides the ability to observe when Envoy has completed its setup and is
ready to start dispatching requests. Any requests sent through Envoy before this setup completes
will be queued automatically, and this function is typically used purely for observability.

**Example**::

  // Kotlin
  builder.setOnEngineRunning { /*do something*/ }

  // Swift
  builder.setOnEngineRunning { /*do something*/ }

~~~~~~~~~~~~~
``setLogger``
~~~~~~~~~~~~~

Specify a closure to be called when Envoy's engine emits a log message.

**Example**::

  // Kotlin
  // This interface is pending for Kotlin
  builder.setLogger { level, message -> /* log it */ }

  // Swift
  builder.setLogger { level, msg in
    NSLog("Envoy log: \(msg)")
  }

~~~~~~~~~~~~~~~~~~~
``setEventTracker``
~~~~~~~~~~~~~~~~~~~

Specify a closure to be called when Envoy's engine emits an event.

**Example**::

  // Kotlin
  builder.setEventTracker ({
    // Track the events. Events are passed in as Map<String, String>.
  })

  // Swift
  builder.setEventTracker { event in
    NSLog("Envoy log: \(event)")
  }

~~~~~~~~~~~~~~~~~~~~~
``addStringAccessor``
~~~~~~~~~~~~~~~~~~~~~

Specify a closure to be called by Envoy to access arbitrary strings from Platform runtime.

**Example**::

  // Kotlin
  builder.addStringAccessor("demo-accessor", { "PlatformString" })

  // Swift
  builder.addStringAccessor(name: "demo-accessor", accessor: { return "PlatformString" })

~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``setNetworkMonitoringMode``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Configure how the engine observes network reachability state changes to update the preferred Envoy network cluster (e.g. WLAN vs WWAN).
Defaults to ``NWPathMonitor``, but can be configured to use ``SCNetworkReachability`` or be disabled completely.

**Example**::

  // Kotlin
  // N/A

  // Swift
  builder.setNetworkMonitoringMode(.pathMonitor)

~~~~~~~~~~~~~~~~~~~~~~~~~~~
``enableGzipDecompression``
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Specify whether to enable transparent response Gzip decompression. Defaults to true.

**Example**::

  // Kotlin
  builder.enableGzipDecompression(false)

  // Swift
  builder.enableGzipDecompression(false)

Default values from the `gzip decompressor proto <https://www.envoyproxy.io/docs/envoy/latest/api-v3/extensions/compression/gzip/decompressor/v3/gzip.proto>`_
are used.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``enableBrotliDecompression``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Specify whether to enable transparent response Brotli decompression. Defaults to false.

**Example**::

  // Kotlin
  builder.enableBrotliDecompression(true)

  // Swift
  builder.enableBrotliDecompression(true)

Default values from the `brotli decompressor proto <https://www.envoyproxy.io/docs/envoy/latest/api-v3/extensions/compression/brotli/decompressor/v3/brotli.proto>`_
are used.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``enableHttp3``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Specify whether to enable HTTP/3. Defaults to true. Only available when the Envoy Mobile build has HTTP/3 included.
When HTTP/3 is enabled, the client will first talk HTTP/2 with the servers and upon receiving alt-svc in the response,
following traffic will be sent via HTTP/3.

**Example**::

  // Kotlin
  builder.enableHttp3(true)

  // Swift
  builder.enableHttp3(true)

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``addQuicHint``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add a host port pair that's known to support QUIC. Only available when HTTP/3 is enabled.
It can be called multiple times to append a list of QUIC hints.
This allows HTTP/3 to be used for the first request to the hosts and avoid the HTTP/2 -> HTTP/3 switch as mentioned in enableHttp3.

**Example**::

  // Kotlin
  builder.addQuicHint("www.example.com", 443)

  // Swift
  builder.addQuicHint("www.example.com", 443)

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``addQuicCanonicalSuffix``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add a canonical suffix that's known to speak QUIC.
This feature works as a extension to QUIC hints in such way that:
if `.abc.com` is added to canonical suffix, and `foo.abc.com` is added to QUIC hint, then all requests to
`*.abc.com` will be considered QUIC ready.

**Example**::

  // Kotlin
  builder.addQuicCanonicalSuffix(".example.com")

  // Swift
  builder.addQuicCanonicalSuffix(".example.com")

~~~~~~~~~~~~~~~~~~~~~~~
``enableSocketTagging``
~~~~~~~~~~~~~~~~~~~~~~~

Specify whether to enable support for Android socket tagging. Unavailable on iOS. Defaults to false.

**Example**::

  // Kotlin
  builder.enableSocketTagging(true)

~~~~~~~~~~~~~~~~~~~~~~~~~~
``enableInterfaceBinding``
~~~~~~~~~~~~~~~~~~~~~~~~~~

Specify whether sockets may attempt to bind to a specific interface, based on network conditions.

**Example**::

  // Kotlin
  builder.enableInterfaceBinding(true)

  // Swift
  builder.enableInterfaceBinding(true)


~~~~~~~~~~~~~~~~~~~~
``addKeyValueStore``
~~~~~~~~~~~~~~~~~~~~

Implementations of a public KeyValueStore interface may be added in their respective languages and
made available to the library. General usage is supported, but typical future usage will be in
support of HTTP and endpoint property caching.

**Example**::

  // Kotlin
  builder.addKeyValueStore("io.envoyproxy.envoymobile.MyKeyValueStore", MyKeyValueStoreImpl())

  // Swift
  // Coming soon.


The library also contains a simple Android-specific KeyValueStore implementation based on Android's
SharedPreferences.

**Example**::

  // Android
  val preferences = context.getSharedPreferences("io.envoyproxy.envoymobile.MyPreferences", Context.MODE_PRIVATE)
  builder.addKeyValueStore("io.envoyproxy.envoymobile.MyKeyValueStore", SharedPreferencesStore(preferences))

  // iOS
  // Coming soon.


~~~~~~~~~~~~~~~~~~~~~~~~~~
``forceIPv6``
~~~~~~~~~~~~~~~~~~~~~~~~~~

Specify whether to remap IPv4 addresses to the IPv6 space and always force connections
to use IPv6. Note this is an experimental option and should be enabled with caution.

**Example**::

  // Kotlin
  // No API: always enabled
  // Swift
  builder.forceIPv6(true)


~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``enablePlatformCertificatesValidation``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Specify whether to use platform provided certificate validation interfaces. Currently only supported on Android. Defaults to false.

**Example**::

  // Kotlin
  builder.enablePlatformCertificatesValidation(true)


~~~~~~~~~~~~~~~~~~~~~~~~~~
``enableProxying``
~~~~~~~~~~~~~~~~~~~~~~~~~~
Specify whether to respect system Proxy settings when establishing connections.
Available on Android only.

**Example**::

    // Kotlin
    builder.enableProxying(true)


~~~~~~~~~~~~~~~~~~~~~~~~~~
``enableDNSCache``
~~~~~~~~~~~~~~~~~~~~~~~~~~

Specify whether to enable DNS cache. Note that DNS cache requires an addition of
a key value store named 'reserved.platform_store'.

The interval at which results are saved to the key value store defaults to 1s
but can also be set explicitly.

A maximum of 100 entries will be stored.

**Example**::

  // Kotlin
  builder.enableDNSCache(true, saveInterval: 60)

  // Swift
  builder.enableDNSCache(true, saveInterval: 60)


~~~~~~~~~~~~~~~~~~~~~~~~~~
``addRuntimeGuard``
~~~~~~~~~~~~~~~~~~~~~~~~~~

Adds a runtime guard key value pair to envoy configuration.  The guard is of the short form "feature"
rather than the fully qualified "envoy.reloadable_features.feature"
Note that Envoy will fail to start up in debug mode if an unknown guard is specified.

**Example**::

  // Kotlin
  builder.addRuntimeGuard("feature", true)

  // Swift
  builder.addRuntimeGuard("feature", true)

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``setXds``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Sets the Bootstrap configuration up with `xDS <https://www.envoyproxy.io/docs/envoy/latest/configuration/overview/xds_api#config-overview-ads>`_
to fetch dynamic configuration from an xDS management server. The xDS management server must
support the ADS protocol. At this moment, only the State-of-the-World (SotW) xDS protocol is
supported, not the Delta protocol. The Envoy Mobile client will communicate with the configured
xDS management server over gRPC.

Use the XdsBuilder class to configure the xDS for the Envoy Mobile engine.  For example, the
`addRuntimeDiscoveryService()` method can be used to configure the
`Runtime Discovery Service (RTDS) <https://www.envoyproxy.io/docs/envoy/latest/api-v3/service/runtime/v3/rtds.proto>`_
and the `addClusterDiscoveryService()` method to configure the
`Cluster Discovery Service (CDS) <https://www.envoyproxy.io/docs/envoy/latest/configuration/upstream/cluster_manager/cds>`_.

Parameters:
xds_builder

**Example**::

  // Kotlin
  val xdsBuilder = new XdsBuilder(address = "my_xds_server.com", port = 443)
                       .addRuntimeDiscoveryService("my_rtds_resource")
                       .addClusterDiscoveryService()
  builder.setXds(xdsBuilder)

  // Swift
  var xdsBuilder = XdsBuilder(address: "my_xds_server.com", port: 443)
                       .addRuntimeDiscoveryService("my_rtds_resource")
                       .addClusterDiscoveryService()
  builder.setXds(xdsBuilder)

  // C++
  XdsBuilder xds_builder(/*address=*/"my_xds_server.com", /*port=*/443);
  xds_builder.addRuntimeDiscoveryService("my_rtds_resource")
      .addClusterDiscoveryService();
  builder.setXds(std::move(xds_builder));

~~~~~~~~~~~~~~~~~~~~~~~~~~
``setNodeId``
~~~~~~~~~~~~~~~~~~~~~~~~~~

Sets the node.id field. See the following link for details:
https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/core/v3/base.proto#envoy-v3-api-msg-config-core-v3-node

**Example**::

  // Kotlin
  builder.setNodeId(nodeId = "my_test_node")

  // Swift
  builder.setNodeID("my_test_node")

  // C++
  builder.setNodeId("my_test_node")

~~~~~~~~~~~~~~~~~~~~~~~~~~
``setNodeLocality``
~~~~~~~~~~~~~~~~~~~~~~~~~~

Sets the node.locality field. See the following link for details:
https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/core/v3/base.proto#envoy-v3-api-msg-config-core-v3-node

**Example**::

  // Kotlin
  builder.setNodeLocality(region = "us-west-1", zone = "some_zone", subZone = "some_sub_zone")

  // Swift
  builder.setNodeLocality(region: "us-west-1", zone: "some_zone", subZone: "some_sub_zone")

  // C++
  builder.setNodeLocality("us-west-1", "some_zone", "some_sub_zone");

----------------------
Advanced configuration
----------------------

In most cases, the functions provided by the builder should cover basic setup requirements.
However, in some cases it can be useful to provide a
`Envoy configuration YAML file <https://www.envoyproxy.io/docs/envoy/latest/configuration/configuration>`_
with additional customizations applied.

This may be done by initializing a builder with the contents of the YAML file you you wish to use:

**Kotlin example**::

  val streamClient = AndroidEngineBuilder(baseContext, Yaml(yamlFileString))
    .setLogLevel(LogLevel.WARN)
    ...
    .build()
    .streamClient()

**Swift example**::

  let streamClient = try EngineBuilder(yaml: yamlFileString)
    .setLogLevel(.warn)
    ...
    .build()
    .streamClient()

.. attention::

  Using custom YAML configurations can lead to runtime bugs or crashes due to the fact that the
  configuration string is not evaluated until runtime, and not all of the core Envoy configuration
  options are supported by Envoy Mobile.

---------------
Making requests
---------------

Now that you have a stream client instance, you can start making requests:

- :ref:`HTTP requests and streams <api_http>`
- :ref:`gRPC streams <api_grpc>`
