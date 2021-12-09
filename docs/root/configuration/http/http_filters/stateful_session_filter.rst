.. _config_http_filters_stateful_session:

Stateful session
================

Stateful session is an HTTP filter which sets an override host based on the extensible session state
and updates the session state based on the final selected upstream host. The override host will
eventually overwrites the load balancing result. This filter can implement session sticky without using
a hash-based load balancer.
And by extending the session state, this filter also allows more flexible control over the results of
the load balancing.


Overview
--------

Session sticky is an important feature of LB. Requests belonging to the same session should be guaranteed
to be routed to the same backend host.
HTTP session sticky of Envoy is generally achieved through hash-based load balancer. And the filter is a
supplement. We should use the filter in the following cases:

* The case where more stable session sticky is required. For example, when a host is marked as degrade and
  it is hoped that the requests for existing sessions will not be affected. At this time, stateful session
  filter is needed.
* The case where a non hash-based load balancer (Random, Round Robin, etc.) is used and session sticky
  is still required. If stateful session is enabled in this case, new requests will be routed to the
  corresponding upstream host based on the result of load balancing. The requests belonging to specific
  session will be routed to specific host.


Configuration
-------------

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.stateful_session.v3.StatefulSession>`
* This filter should be configured with the name *envoy.filters.http.stateful_session*.

How it works
------------

Stateful session configuration consists of two parts. An :ref:`extensible session state
<envoy_v3_api_field_extensions.filters.http.stateful_session.v3.StatefulSession.session_state>`.
The expected :ref:`upstream host statuses
<envoy_v3_api_field_extensions.filters.http.stateful_session.v3.StatefulSession.host_statuses>`.

In the process of processing the request, the session state will search for the corresponding session and
host according to the request. If it is a complete new request, the session state will create a session
and save the corresponding host when responding. Please note that the session here is an abstract concept.
In different session state implementations, the session may be completely different.

The host statuses is an expectation. The override host from session state and this expectation eventually
will be passed to the load balancer. If the override host exists and its current health status matches this
expectation then the load balancer will return the host directly instead of performing the load balancing.

One example
___________

Currently, only :ref:`cookie-based session state
<envoy_v3_api_msg_extensions.http.stateful_session.cookie.v3.CookieBasedSessionState>` is supported.
So let's take this as an example.

.. code-block:: yaml

  name: envoy.filters.http.stateful_session
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.stateful_session.v3.StatefulSession
    host_statuses:
    - HEALTHY
    - DEGRADED
    session_state:
      name: envoy.http.stateful_session.cookie
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.stateful_session.cookie.v3.CookieBasedSessionState
        name: global-session-cookie
        path: /path
        ttl: 120s


In the above configuration, the cookie-based session state obtains the override host of the current session
from the cookie by the key `global-session-cookie` and if the corresponding host exists and its health status
is `HEALTHY` or `DEGRADED`, the corresponding request will be routed to the specified override host.
