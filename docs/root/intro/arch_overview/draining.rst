.. _arch_overview_draining:

Draining
========

Draining is the process by which Envoy attempts to gracefully shed connections in response to
various events. Draining occurs at the following times:

* The server has been manually health check failed via the :ref:`healthcheck/fail
  <operations_admin_interface_healthcheck_fail>` admin endpoint. See the :ref:`health check filter
  <arch_overview_health_checking_filter>` architecture overview for more information.
* The server is being :ref:`hot restarted <arch_overview_hot_restart>`.
* Individual listeners are being modified or removed via :ref:`LDS
  <arch_overview_dynamic_config_lds>`.

Each :ref:`configured listener <arch_overview_listeners>` has a :ref:`drain_type
<config_listeners_drain_type>` setting which controls when draining takes place. The currently
supported values are:

default
  Envoy will drain listeners in response to all three cases above (admin drain, hot restart, and
  LDS update/remove). This is the default setting.

modify_only
  Envoy will drain listeners only in response to the 2nd and 3rd cases above (hot restart and
  LDS update/remove). This setting is useful if Envoy is hosting both ingress and egress listeners.
  It may be desirable to set *modify_only* on egress listeners so they only drain during
  modifications while relying on ingress listener draining to perform full server draining when
  attempting to do a controlled shutdown.

Note that although draining is a per-listener concept, it must be supported at the network filter
level. Currently the only filters that support graceful draining are
:ref:`HTTP connection manager <config_http_conn_man>`,
:ref:`Redis <config_network_filters_redis_proxy>`, and
:ref:`Mongo <config_network_filters_mongo_proxy>`.
