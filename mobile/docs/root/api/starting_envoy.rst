.. _api_starting_envoy:

Starting Envoy
==============

----------------
``StreamClient``
----------------

Starting an instance of Envoy Mobile for performing requests is done by creating a ``StreamClient``.

To do so, create a ``StreamClientBuilder`` and call ``build()`` (see below).

After the client is created, it should be stored and used to start network requests/streams.

**Kotlin example**::

  val streamClient = AndroidStreamClientBuilder(getApplication())
    .addLogLevel(LogLevel.WARN)
    ...
    .build()

**Swift example**::

  let streamClient = try StreamClientBuilder()
    .addLogLevel(.warn)
    ...
    .build()

-----------------------
``StreamClientBuilder``
-----------------------

This type is used to configure an instance of ``StreamClient`` before finally
creating the client using ``.build()``.

Available builders are 1:1 between iOS/Android, and are documented below.

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

~~~~~~~~~~~~~~~
``addLogLevel``
~~~~~~~~~~~~~~~

Specify the log level to be used when running the underlying Envoy engine.

**Example**::

  // Kotlin
  builder.addLogLevel(LogLevel.WARN)

  // Swift
  builder.addLogLevel(.warn)

~~~~~~~~~~~~~~~~~~
``addStatsDomain``
~~~~~~~~~~~~~~~~~~

Specify a domain which implements the
:tree:`stats endpoint <83908423d46a37574e9a35627df1f3dd9634e5ec/library/common/config_template.cc#L139-L145>`
in order to take advantage of the
`stats emitted by Envoy <https://www.envoyproxy.io/docs/envoy/latest/configuration/upstream/cluster_manager/cluster_stats>`_
(and subsequently Envoy Mobile).

Note that only stats specified in the configuration's
:tree:`whitelist <83908423d46a37574e9a35627df1f3dd9634e5ec/library/common/config_template.cc#L146-L167>`
will be emitted.

**Example**::

  // Kotlin
  builder.addStatsDomain("envoy-mobile.envoyproxy.io")

  // Swift
  builder.addStatsDomain("envoy-mobile.envoyproxy.io")

~~~~~~~~~~~~~~~~~~~~~~~~
``addStatsFlushSeconds``
~~~~~~~~~~~~~~~~~~~~~~~~

Specify the rate at which Envoy Mobile should flush its queued stats.

**Example**::

  // Kotlin
  builder.addStatsFlushSeconds(5L)

  // Swift
  builder.addStatsFlushSeconds(5)

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

----------------------
Advanced configuration
----------------------

In most cases, the functions provided by the builder should cover basic setup requirements.
However, in some cases it can be useful to provide a
`Envoy configuration YAML file <https://www.envoyproxy.io/docs/envoy/latest/configuration/configuration>`_
with additional customizations applied.

This may be done by initializing a builder with the contents of the YAML file you you wish to use:

**Kotlin example**::

  val streamClient = AndroidStreamClientBuilder(baseContext, Yaml(yamlFileString))
    .addLogLevel(LogLevel.WARN)
    .addStatsFlushSeconds(60)
    ...
    .build()

**Swift example**::

  let streamClient = try StreamClientBuilder(yaml: yamlFileString)
    .addLogLevel(.warn)
    .addStatsFlushSeconds(60)
    ...
    .build()

.. attention::

  Using custom YAML configurations can lead to runtime bugs or crashes due to the fact that the
  configuration string is not evaluated until runtime, and not all of the core Envoy configuration
  options are supported by Envoy Mobile.

---------------
Making requests
---------------

Now that you have an Envoy Mobile instance, you can start making requests:

- :ref:`HTTP requests and streams <api_http>`
- :ref:`gRPC streams <api_grpc>`
