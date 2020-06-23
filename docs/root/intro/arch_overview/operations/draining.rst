.. _arch_overview_draining:

Draining
========

In a few different scenarios, Envoy will attempt to gracefully shed connections. For instance,
during server shutdown, existing requests can be discouraged and listeners set to stop accepting,
to reduce the number of open connections when the server shuts down. Draining behaviour is defined
by the server options in addition to individual listener configs.

Draining occurs at the following times:

* The server is being :ref:`hot restarted <arch_overview_hot_restart>`.
* The server begins the graceful drain sequence via the :ref:`drain_listeners?graceful
  <operations_admin_interface_drain>` admin endpoint.
* The server has been manually health check failed via the :ref:`healthcheck/fail
  <operations_admin_interface_healthcheck_fail>` admin endpoint. See the :ref:`health check filter
  <arch_overview_health_checking_filter>` architecture overview for more information.
* Individual listeners are being modified or removed via :ref:`LDS
  <arch_overview_dynamic_config_lds>`.

By default, the Envoy server will close listeners immediately on server shutdown. To drain listeners
for some duration of time prior to server shutdown, use :ref:`drain_listeners <operations_admin_interface_drain>`
before shutting down the server. The listeners will be directly stopped without any graceful draining behaviour,
and cease accepting new connections immediately.

To add a graceful drain period prior to listeners being closed, use the query parameter
:ref:`drain_listeners?graceful <operations_admin_interface_drain>`. By default, Envoy
will discourage requests for some period of time (as determined by :option:`--drain-time-s`). 
The behaviour of request discouraging is determined by the drain manager.

Note that although draining is a per-listener concept, it must be supported at the network filter
level. Currently the only filters that support graceful draining are
:ref:`Redis <config_network_filters_redis_proxy>`,
:ref:`Mongo <config_network_filters_mongo_proxy>`,
and :ref:`HTTP connection manager <config_http_conn_man>`.

By default, the :ref:`HTTP connection manager <config_http_conn_man>` filter will
add "Connection: close" to HTTP1 requests, send HTTP2 GOAWAY, and terminate connections
on request completion (after the delayed close period).

Each :ref:`configured listener <arch_overview_listeners>` has a :ref:`drain_type
<envoy_v3_api_enum_config.listener.v3.Listener.DrainType>` setting which controls when draining takes place. The currently
supported values are:

default
  Envoy will drain listeners in response to all three cases above (admin health fail, hot restart, and
  LDS update/remove). This is the default setting.

modify_only
  Envoy will drain listeners only in response to the 2nd and 3rd cases above (hot restart and
  LDS update/remove). This setting is useful if Envoy is hosting both ingress and egress listeners.
  It may be desirable to set *modify_only* on egress listeners so they only drain during
  modifications while relying on ingress listener draining to perform full server draining when
  attempting to do a controlled shutdown.
