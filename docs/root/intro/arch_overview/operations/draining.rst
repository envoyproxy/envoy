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

To add a graceful drain period prior to listeners being closed, use the query
parameter :ref:`drain_listeners?graceful <operations_admin_interface_drain>`.
By default, Envoy will discourage requests for some period of time (as
determined by :option:`--drain-time-s`) but continue accepting new connections
until the drain timeout. The behaviour of request discouraging is determined by
the drain manager.

Note that although draining is a per-listener concept, it must be supported at the network filter
level. Currently the only filters that support graceful draining are
:ref:`Redis <config_network_filters_redis_proxy>`,
:ref:`Mongo <config_network_filters_mongo_proxy>`,
:ref:`Thrift <config_network_filters_thrift_proxy>`
(if the ``envoy.reloadable_features.thrift_connection_draining`` runtime feature is enabled),
and :ref:`HTTP connection manager <config_http_conn_man>`.

By default, the :ref:`HTTP connection manager <config_http_conn_man>` filter will
add "Connection: close" to HTTP1 requests, send HTTP2 GOAWAY, and terminate connections
on request completion (after the delayed close period).

The HTTP connection manager decides whether to drain a connection in two ways:

* When a response is sent. With the gradual drain strategy (see :option:`--drain-strategy`) the
  probability that a response drains its connection increases linearly from zero at the start of
  the drain sequence to one at half of the :option:`--drain-time-s`. From then on every response
  drains its connection.
* Proactively, for the connections that are still not draining after two thirds of the drain time,
  for example because they are idle or have a low request rate. Each of these connections is
  drained at a random point in the remainder of the drain time, early enough for the :ref:`drain
  timeout
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.drain_timeout>`
  to elapse before the drain sequence ends. Idle connections are closed once the drain timeout
  elapses.

Upgraded (e.g. WebSocket) and ``CONNECT`` streams send no further response and cannot be told to
go away, so they are reset when the connection is drained proactively: on HTTP/1 connections as
soon as that happens, as the connection cannot be reused anyway, and on HTTP/2 and HTTP/3
connections once the drain timeout elapses. Other requests are left to complete. A connection
drained by a response keeps its tunneling streams until it is drained proactively as well.

Spreading the proactive draining over the last third of the drain time avoids closing all the
remaining connections at the same moment at the end of the drain sequence, which would make their
clients reconnect at the same time. For the same reason connections are not drained proactively
when the drain time is too short to leave room for it after the drain timeout, nor are connections
accepted after the last third of the drain time has begun. Connections are also not drained
proactively when the server has been health check failed, as that has no drain time.

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

.. note::

  Envoy also drains upstream connections when the upstream clusters are modified. The behavior
  depends on the protocols used for the connection pools, and is currently passive: Envoy stops
  issuing streams to the connection pools associated with the removed clusters, and waits for the
  existing streams to complete.

