.. _config_health_checkers_multi:

Multiple Health Checkers
========================

The Multiple Health Checker runs multiple health checks against each upstream host. All configured health
checks must pass for a host to be considered healthy. If any single health check fails, the host is marked
unhealthy.

Each sub-checker is configured with a full
:ref:`HealthCheck <envoy_v3_api_msg_config.core.v3.HealthCheck>` configuration, allowing
independent control of timing, thresholds, and transport socket settings per health check method.

An optional :ref:`name <envoy_v3_api_field_extensions.health_checkers.multi.v3.Multi.HealthCheck.name>`
can be set on each sub-checker to identity the results from each sub-checker. If set, stats for that
sub-checker will appear under ``health_check.name.<name>.health_check.{attempt,success,...}``
instead of the default shared ``health_check.{attempt,success,...}``.

.. note::

  The timing and threshold fields in the enclosing
  :ref:`HealthCheck <envoy_v3_api_field_config.cluster.v3.Cluster.health_checks>` entry that wraps
  the multi checker are not used. Only the configuration of each sub-checker is used.

An example for :ref:`custom_health_check <envoy_v3_api_msg_config.core.v3.HealthCheck.CustomHealthCheck>`
using the multi health checker is shown below:

.. code-block:: yaml

  custom_health_check:
    name: envoy.health_checkers.multi
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.health_checkers.multi.v3.Multi
      health_checks:
      - name: http
        health_check:
          timeout: 1s
          interval: 5s
          unhealthy_threshold: 3
          healthy_threshold: 2
          http_health_check:
            path: /healthcheck
      - name: tcp
        health_check:
          timeout: 1s
          interval: 5s
          unhealthy_threshold: 3
          healthy_threshold: 2
          tcp_health_check: {}

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.health_checkers.multi.v3.Multi>`
