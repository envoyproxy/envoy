.. _api_starting_envoy:

Starting Envoy
==============

---------------
``EnvoyClient``
---------------

Starting an instance of Envoy Mobile for making requests is done by creating an ``EnvoyClient``,
which conforms to the ``HTTPClient`` interface.

To do so, create an ``EnvoyClientBuilder`` and call ``build()``.

This builder exposes some of Envoy's configuration options programmatically using builder
functions, some of which are demonstrated below:

**Kotlin example**::

  val envoy = AndroidEnvoyClientBuilder(baseContext)
    .addLogLevel(LogLevel.WARN)
    .addStatsFlushSeconds(60)
    ...
    .build()

**Swift example**::

  let envoy = try EnvoyClientBuilder()
    .addLogLevel(.warn)
    .addStatsFlushSeconds(60)
    ...
    .build()

After the client is created, it should be stored and kept in memory in order to be used
for issuing requests.

----------------------
Advanced configuration
----------------------

In most cases, the functions provided by the builder should cover basic setup requirements.
However, in some cases it can be useful to provide a
`Envoy configuration YAML file <https://www.envoyproxy.io/docs/envoy/latest/configuration/configuration>`_
with additional customizations applied.

This may be done by initializing a builder with the contents of the YAML file you you wish to use:

**Kotlin example**::

  val envoy = AndroidEnvoyClientBuilder(baseContext, Yaml(yamlFileString))
    .addLogLevel(LogLevel.WARN)
    .addStatsFlushSeconds(60)
    ...
    .build()

**Swift example**::

  let envoy = try EnvoyClientBuilder(yaml: yamlFileString)
    .addLogLevel(.warn)
    .addStatsFlushSeconds(60)
    ...
    .build()


.. attention::

  Using custom YAML configurations can lead to runtime bugs or crashes due to the fact that the
  configuration string is not evaluated until runtime, and not all of the core Envoy configuration
  options are supported by Envoy Mobile.

Making Requests
---------------

Now that you have an Envoy Mobile instance, you can start making requests:

- :ref:`HTTP requests and streams <api_http>`
- :ref:`gRPC streams <api_grpc>`
