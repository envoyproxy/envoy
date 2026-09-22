.. _config_health_checkers_multi:

Multiple Health Checkers
========================

When a cluster has more than one entry in the
:ref:`health_checks <envoy_v3_api_field_config.cluster.v3.Cluster.health_checks>` list,
Envoy automatically runs all of them against each upstream host. All configured health
checks must pass for a host to be considered healthy. If any single health check fails,
the host is marked unhealthy.

When multiple health checks are configured, each entry must have a
:ref:`name <envoy_v3_api_field_config.core.v3.HealthCheck.name>`. Stats for each
checker will appear under ``health_check.name.<name>.health_check.{attempt,success,...}``
instead of the default shared ``health_check.{attempt,success,...}``.

An example cluster configuration with multiple health checks is shown below:

.. code-block:: yaml

  clusters:
  - name: my_cluster
    health_checks:
    - timeout: 1s
      interval: 5s
      unhealthy_threshold: 3
      healthy_threshold: 2
      name: my_http
      http_health_check:
        path: /healthcheck
    - timeout: 1s
      interval: 5s
      unhealthy_threshold: 3
      healthy_threshold: 2
      name: my_tcp
      tcp_health_check: {}
