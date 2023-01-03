.. _config_health_checkers_cached:

Cached Health Checker
=====================

The Cached Health Checker (with :code:`envoy.health_checkers.cached` as name) subscribe to a redis cache for upstream host health check notification.
Once the health check result is set in the redis cache by a third party checker, a keyspace set event is recevied from the redis cache, then it get
the health check result and store it in a in memory cache.


An example for :ref:`custom_health_check <envoy_v3_api_msg_config.core.v3.HealthCheck.CustomHealthCheck>`
using the Cached health checker is shown below:


.. code-block:: yaml

  custom_health_check:
    name: envoy.health_checkers.cached
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.cached.v3.Cached
        host: localhost
        port: 6400
        password: foobared
        db: 100
        tls_options:
          enabled: true
          cacert: /etc/redis/ca.crt
          cert: /etc/redis/client.crt
          key: /etc/redis/client.key

* :ref:`v3 API reference <envoy_v3_api_msg_config.core.v3.HealthCheck.CustomHealthCheck>`
