.. _config_http_filters_stateful_session:

Stateful session
================

The stateful session filter overrides the upstream host based on extensible session state
and updates the session state based on the final selected upstream host. The override takes
precedence over the result of load balancing. This filter implements session stickiness without
relying on a hash-based load balancer.

By extending the session state, this filter also allows more flexible control over load balancing
results.

.. note::

  Stateful sessions can result in imbalanced load across upstreams and allow external actors to direct
  requests to specific upstream hosts. Operators should carefully consider the security and reliability
  implications of stateful sessions before enabling this feature.

Overview
--------

Session stickiness allows requests belonging to the same session to be consistently routed to a specific
upstream host.

HTTP session stickiness in Envoy is generally achieved through hash-based load balancing.
The stickiness of hash-based sessions is considered 'weak' because the upstream host may change when the
host set changes. This filter implements 'strong' stickiness. It is intended to handle the following cases:

* The case where more stable session stickiness is required. For example, when a host is marked as degraded
  but it is desirable to continue routing requests for existing sessions to that host.
* The case where a non-hash-based load balancer (Random, Round Robin, etc.) is used and session stickiness
  is still required. If stateful sessions are enabled in this case, requests for new sessions will be routed
  to the corresponding upstream host based on the result of load balancing. Requests belonging to existing
  sessions will be routed to the session's upstream host.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.stateful_session.v3.StatefulSession``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.stateful_session.v3.StatefulSession>`

How it works
------------

The most important configuration for this filter is an :ref:`extensible session state
<envoy_v3_api_field_extensions.filters.http.stateful_session.v3.StatefulSession.session_state>`.

While processing the request, the stateful session filter will search for the corresponding session and
host based on the request. The results of the search will be used to influence the final load balancing
results.

If no existing session is found, the filter will create a session to store the selected upstream host.
Please note that the session here is an abstract concept. The details of the storage are based on the
session state implementation.

Examples
________

Currently, :ref:`cookie-based session state
<envoy_v3_api_msg_extensions.http.stateful_session.cookie.v3.CookieBasedSessionState>` and :ref:`header-based session state
<envoy_v3_api_msg_extensions.http.stateful_session.header.v3.HeaderBasedSessionState>` are supported.
The following shows a cookie-based configuration.

.. literalinclude:: _include/stateful-cookie-session.yaml
    :language: yaml
    :lines: 28-44
    :emphasize-lines: 4-14
    :linenos:
    :lineno-start: 28
    :caption: :download:`stateful-cookie-session.yaml <_include/stateful-cookie-session.yaml>`

In the above configuration, the cookie-based session state obtains the overridden host of the current session
from the cookie named ``global-session-cookie`` and, if the corresponding host exists in the upstream cluster, the
request will be routed to that host.

If there is no valid cookie, the load balancer will choose a new upstream host. When responding, the address
of the selected upstream host will be stored in the cookie named ``global-session-cookie``.

A similar example for a header-based configuration is:

.. literalinclude:: _include/stateful-header-session.yaml
    :language: yaml
    :lines: 28-41
    :emphasize-lines: 4-11
    :linenos:
    :lineno-start: 28
    :caption: :download:`stateful-header-session.yaml <_include/stateful-header-session.yaml>`

.. note::
  The header-based implementation assumes that a client will use the last supplied value for the session
  header and will pass it with every subsequent request.

  ``StatefulSessionPerRoute`` should be used if path match is required.

Statistics
----------

This filter outputs statistics in the
``http.<stat_prefix>.stateful_session.`` namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

When :ref:`stat_prefix
<envoy_v3_api_field_extensions.filters.http.stateful_session.v3.StatefulSession.stat_prefix>` is not
configured on the filter, no statistics are emitted.

If :ref:`stat_prefix
<envoy_v3_api_field_extensions.filters.http.stateful_session.v3.StatefulSession.stat_prefix>` is
configured on the filter, an additional segment is inserted after ``stateful_session`` to allow
distinguishing statistics from multiple instances, e.g. ``http.<stat_prefix>.stateful_session.my_prefix.routed``.

.. note::

  Per-route configuration overrides do not support statistics and will not emit statistics even if
  :ref:`stat_prefix
  <envoy_v3_api_field_extensions.filters.http.stateful_session.v3.StatefulSession.stat_prefix>` is set in the
  per-route configuration.

The following statistics are supported:

.. csv-table::
  :header: Name, Type, Description
  :widths: auto

  routed, Counter, "Total requests where a stateful session override was attempted and
  successfully applied and the selected upstream matched the requested session destination."
  failed_open, Counter, "Total requests where an override was attempted but the requested destination
  was unavailable and the request proceeded using default load balancing (``strict`` is ``false``)."
  failed_closed, Counter, "Total requests where an override was attempted but the requested
  destination was unavailable and the request was fail-closed with a ``503`` (``strict`` is ``true``)."
  no_session, Counter, "Total requests that reached an upstream without session state when the filter
  is active. This includes requests with no session cookie/header or where session extraction failed.
  It excludes requests where the filter is explicitly disabled per-route."
