.. _config_health_checkers_multi:

Multiple Health Checkers
========================

When a cluster has more than one entry in the
:ref:`health_checks <envoy_v3_api_field_config.cluster.v3.Cluster.health_checks>` list,
Envoy automatically runs all of them against each upstream host. All configured health
checks must pass for a host to be considered healthy. If any single health check fails,
the host is marked unhealthy.

Each health check entry is a full
:ref:`HealthCheck <envoy_v3_api_msg_config.core.v3.HealthCheck>` configuration, allowing
independent control of timing, thresholds, and transport socket settings per health check method.

An optional :ref:`name <envoy_v3_api_field_config.core.v3.HealthCheck.name>`
can be set on each health check to identify the results from each checker. If set, stats for that
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
      name: http
      http_health_check:
        path: /healthcheck
    - timeout: 1s
      interval: 5s
      unhealthy_threshold: 3
      healthy_threshold: 2
      name: tcp
      tcp_health_check: {}
