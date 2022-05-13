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
    .addLogLevel(LogLevel.WARN)
    ...
    .build()
    .streamClient()

**Swift example**::

  let streamClient = try EngineBuilder()
    .addLogLevel(.warn)
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

.. attention::

  This API is non-ideal as it exposes lower-level internals of Envoy than desired by this
  project.
  :issue:`#1581 <1581>` tracks enhancing this API.

Add a list of hostnames to preresolve on Engine startup.
The configuration is expected as a JSON list.

  // Kotlin
  builder.addDNSPreresolveHostnames("[{\"address\": \"foo.com", \"port_value\": 443}]")

  // Swift
  builder.addDNSPreresolveHostnames("[{\"address\": \"foo.com", \"port_value\": 443}]")

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``addDNSFallbackNameservers``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. attention::

  This API is only available for Kotlin.

Add a list of IP addresses to use as fallback DNS name servers.
See `the Envoy docs <https://www.envoyproxy.io/docs/envoy/latest/api-v3/extensions/network/dns_resolver/cares/v3/cares_dns_resolver.proto#extensions-network-dns-resolver-cares-v3-caresdnsresolverconfig>`__
for further information.

  // Kotlin
  builder.addDNSFallbackNameservers(listOf<String>("8.8.8.8"))

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``enableDNSFilterUnroutableFamilies``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. attention::

  This API is only available for Kotlin.

Specify whether to filter unroutable IP families during DNS resolution or not.
See `the Envoy docs <https://www.envoyproxy.io/docs/envoy/latest/api-v3/extensions/network/dns_resolver/cares/v3/cares_dns_resolver.proto#extensions-network-dns-resolver-cares-v3-caresdnsresolverconfig>`__
for further information.

  // Kotlin
  builder.enableDNSFilterUnroutableFamilies(true)

~~~~~~~~~~~~~~~
``addLogLevel``
~~~~~~~~~~~~~~~

Specify the log level to be used when running the underlying Envoy engine.

**Example**::

  // Kotlin
  builder.addLogLevel(LogLevel.WARN)

  // Swift
  builder.addLogLevel(.warn)

~~~~~~~~~~~~~~~~~~~~~~
``addGrpcStatsDomain``
~~~~~~~~~~~~~~~~~~~~~~

Specify a domain which implements the
:tree:`stats endpoint <83908423d46a37574e9a35627df1f3dd9634e5ec/library/common/config_template.cc#L139-L145>`
in order to take advantage of the
`stats emitted by Envoy <https://www.envoyproxy.io/docs/envoy/latest/configuration/upstream/cluster_manager/cluster_stats>`_
(and subsequently Envoy Mobile).

Note that only stats specified in the configuration's
:tree:`inclusion list <83908423d46a37574e9a35627df1f3dd9634e5ec/library/common/config_template.cc#L146-L167>`
will be emitted.

Passing ``nil``/``null`` disables stats emission, and this is the default value.

**Example**::

  // Kotlin
  builder.addGrpcStatsDomain("envoy-mobile.envoyproxy.io")

  // Swift
  builder.addGrpcStatsDomain("envoy-mobile.envoyproxy.io")

~~~~~~~~~~~~~~~~~~~~~~~~
``addStatsFlushSeconds``
~~~~~~~~~~~~~~~~~~~~~~~~

Specify the rate at which Envoy Mobile should flush its queued stats.

**Example**::

  // Kotlin
  builder.addStatsFlushSeconds(5L)

  // Swift
  builder.addStatsFlushSeconds(5)

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
This information is sent as metadata when flushing stats.

**Example**::

  // Kotlin
  builder.addAppVersion("v1.2.3")

  // Swift
  builder.addAppVersion("v1.2.3")

~~~~~~~~~~~~
``addAppId``
~~~~~~~~~~~~

Specify the version of the app using Envoy Mobile.
This information is sent as metadata when flushing stats.

**Example**::

  // Kotlin
  builder.addAppId("com.mydomain.myapp")

  // Swift
  builder.addAppId("com.mydomain.myapp)

~~~~~~~~~~~~~~~~~~~~~~
``addVirtualClusters``
~~~~~~~~~~~~~~~~~~~~~~

Specify the virtual clusters config for Envoy Mobile's configuration.
The configuration is expected as a JSON list.
This functionality is used for stat segmentation.

.. attention::

    This API is non-ideal as it exposes lower-level internals of Envoy than desired by this project.
    :issue:`#770 <770>` tracks enhancing this API.

**Example**::

  // Kotlin
  builder.addVirtualClusters("[{\"name\":\"vcluster\",\"headers\":[{\"name\":\":path\",\"exact_match\":\"/v1/vcluster\"}]}]")

  // Swift
  builder.addVirtualClusters("[{\"name\":\"vcluster\",\"headers\":[{\"name\":\":path\",\"exact_match\":\"/v1/vcluster\"}]}]")

~~~~~~~~~~~~~~~~~~~~~~~~~
``enableAdminInterface``
~~~~~~~~~~~~~~~~~~~~~~~~~

Enable admin interface on 127.0.0.1:9901 address.

.. attention::

    Admin interface is intended to be used for development/debugging purposes only.
    Enabling it in production may open your app to security vulnerabilities.

**Example**::

  // Kotlin
  builder.enableAdminInterface()

  // Swift
  builder.enableAdminInterface()

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

  // Swift
  builder.setLogger { msg in
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
``enableNetworkPathMonitor``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Configure the engine to use ``NWPathMonitor`` rather than ``SCNetworkReachability``
on supported platforms (iOS 12+) to update the preferred Envoy network cluster (e.g. WLAN vs WWAN).

.. attention::

    Only available on iOS 12 or later.

**Example**::

  // Kotlin
  // N/A

  // Swift
  builder.enableNetworkPathMonitor()

~~~~~~~~~~~~~~~~~~~~~~~
``enableHappyEyeballs``
~~~~~~~~~~~~~~~~~~~~~~~

Specify whether to use Happy Eyeballs when multiple IP stacks may be supported. Defaults to true.

**Example**::

  // Kotlin
  builder.enableHappyEyeballs(true)

  // Swift
  builder.enableHappyEyeballs(true)

~~~~~~~~~~~~~~~~~~~~~~~~~~
``enableInterfaceBinding``
~~~~~~~~~~~~~~~~~~~~~~~~~~

Specify whether sockets may attempt to bind to a specific interface, based on network conditions.

**Example**::

  // Kotlin
  builder.enableInterfaceBinding(true)

  // Swift
  builder.enableInterfaceBinding(true)

~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``h2ExtendKeepaliveTimeout``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Extend the keepalive timeout when *any* frame is received on the owning HTTP/2 connection.

This can help negate the effect of head-of-line (HOL) blocking for slow connections.

**Example**::

  // Kotlin
  builder.h2ExtendKeepaliveTimeout(true)

  // Swift
  builder.h2ExtendKeepaliveTimeout(true)

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
    .addLogLevel(LogLevel.WARN)
    .addStatsFlushSeconds(60)
    ...
    .build()
    .streamClient()

**Swift example**::

  let streamClient = try EngineBuilder(yaml: yamlFileString)
    .addLogLevel(.warn)
    .addStatsFlushSeconds(60)
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
