.. _arch_overview_http_conn_man:

HTTP connection management
==========================

HTTP is such a critical component of modern service oriented architectures that Envoy implements a
large amount of HTTP specific functionality. Envoy has a built in network level filter called the
:ref:`HTTP connection manager <config_http_conn_man>`. This filter translates raw bytes into HTTP
level messages and events (e.g., headers received, body data received, trailers received, etc.). It
also handles functionality common to all HTTP connections and requests such as :ref:`access logging
<arch_overview_access_logs>`, :ref:`request ID generation and tracing <arch_overview_tracing>`,
:ref:`request/response header manipulation <config_http_conn_man_headers>`, :ref:`route table
<arch_overview_http_routing>` management, and :ref:`statistics <config_http_conn_man_stats>`.

HTTP connection manager :ref:`configuration <config_http_conn_man>`.

.. _arch_overview_http_protocols:

HTTP protocols
--------------

Envoy’s HTTP connection manager has native support for HTTP/1.1, WebSockets, and HTTP/2. It does not support
SPDY. Envoy’s HTTP support was designed to first and foremost be an HTTP/2 multiplexing proxy.
Internally, HTTP/2 terminology is used to describe system components. For example, an HTTP request
and response take place on a *stream*. A codec API is used to translate from different wire
protocols into a protocol agnostic form for streams, requests, responses, etc. In the case of
HTTP/1.1, the codec translates the serial/pipelining capabilities of the protocol into something
that looks like HTTP/2 to higher layers. This means that the majority of the code does not need to
understand whether a stream originated on an HTTP/1.1 or HTTP/2 connection.

HTTP header sanitizing
----------------------

The HTTP connection manager performs various :ref:`header sanitizing
<config_http_conn_man_header_sanitizing>` actions for security reasons.

Route table configuration
-------------------------

Each :ref:`HTTP connection manager filter <config_http_conn_man>` has an associated :ref:`route
table <arch_overview_http_routing>`. The route table can be specified in one of two ways:

* Statically.
* Dynamically via the :ref:`RDS API <config_http_conn_man_rds>`.

.. _arch_overview_http_retry_plugins:

Retry plugin configuration
--------------------------

Normally during retries, host selection follows the same process as the original request. Retry plugins
can be used to modify this behavior, and they fall into two categories:

* :ref:`Host Predicates <envoy_api_field_route.RetryPolicy.retry_host_predicate>`:
  These predicates can be used to "reject" a host, which will cause host selection to be reattempted.
  Any number of these predicates can be specified, and the host will be rejected if any of the predicates reject the host.

  Envoy supports the following built-in host predicates

  * *envoy.retry_host_predicates.previous_hosts*: This will keep track of previously attempted hosts, and rejects
    hosts that have already been attempted.

  * *envoy.retry_host_predicates.omit_canary_hosts*: This will reject any host that is a marked as canary host.
    Hosts are marked by setting ``canary: true`` for the ``envoy.lb`` filter in the endpoint's filter metadata.
    See :ref:`LbEndpoint <envoy_api_msg_endpoint.LbEndpoint>` for more details.

  * *envoy.retry_host_predicates.omit_host_metadata*: This will reject any host based on predefined metadata match criteria. 
    See the configuration example below for more details.

* :ref:`Priority Predicates<envoy_api_field_route.RetryPolicy.retry_priority>`: These predicates can
  be used to adjust the priority load used when selecting a priority for a retry attempt. Only one such
  predicate may be specified.

  Envoy supports the following built-in priority predicates

  * *envoy.retry_priority.previous_priorities*: This will keep track of previously attempted priorities,
    and adjust the priority load such that other priorities will be targeted in subsequent retry attempts.

Host selection will continue until either the configured predicates accept the host or a configurable
:ref:`max attempts <envoy_api_field_route.RetryPolicy.host_selection_retry_max_attempts>` has been reached.

These plugins can be combined to affect both host selection and priority load. Envoy can also be extended
with custom retry plugins similar to how custom filters can be added.


**Configuration Example**

For example, to configure retries to prefer hosts that haven't been attempted already, the built-in
``envoy.retry_host_predicates.previous_hosts`` predicate can be used:

.. code-block:: yaml

  retry_policy:
    retry_host_predicate:
    - name: envoy.retry_host_predicates.previous_hosts
    host_selection_retry_max_attempts: 3

This will reject hosts previously attempted, retrying host selection a maximum of 3 times. The bound
on attempts is necessary in order to deal with scenarios in which finding an acceptable host is either
impossible (no hosts satisfy the predicate) or very unlikely (the only suitable host has a very low
relative weight).

To reject a host based on its metadata, ``envoy.retry_host_predicates.omit_host_metadata`` can be used:

.. code-block:: yaml

  retry_policy:
    retry_host_predicate:
    - name: envoy.retry_host_predicates.omit_host_metadata
      typed_config:
        "@type": type.googleapis.com/envoy.config.retry.omit_host_metadata.v2.OmitHostMetadataConfig
        metadata_match:
          filter_metadata:
            envoy.lb:
              key: value

This will reject any host with matching (key, value) in its metadata.

To configure retries to attempt other priorities during retries, the built-in
``envoy.retry_priority.previous_priorities`` can be used.

.. code-block:: yaml

  retry_policy:
    retry_priority:
      name: envoy.retry_priorities.previous_priorities
      typed_config:
        "@type": type.googleapis.com/envoy.config.retry.previous_priorities.PreviousPrioritiesConfig
        update_frequency: 2

This will target priorities in subsequent retry attempts that haven't been already used. The ``update_frequency`` parameter decides how
often the priority load should be recalculated.

These plugins can be combined, which will exclude both previously attempted hosts as well as
previously attempted priorities.

.. code-block:: yaml

  retry_policy:
    retry_host_predicate:
    - name: envoy.retry_host_predicates.previous_hosts
    host_selection_retry_max_attempts: 3
    retry_priority:
      name: envoy.retry_priorities.previous_priorities
      typed_config:
        "@type": type.googleapis.com/envoy.config.retry.previous_priorities.PreviousPrioritiesConfig
        update_frequency: 2

.. _arch_overview_internal_redirects:

Internal redirects
--------------------------

Envoy supports handling 302 redirects internally, that is capturing a 302 redirect response,
synthesizing a new request, sending it to the upstream specified by the new route match, and
returning the redirected response as the response to the original request.

Internal redirects are configured via the ref:`internal redirect action
<envoy_api_field_route.RouteAction.internal_redirect_action>` field and
`max internal redirects <envoy_api_field_route.RouteAction.max_internal_redirects>` field in
route configuration. When redirect handling is on, any 302 response from upstream is
subject to the redirect being handled by Envoy.

For a redirect to be handled successfully it must pass the following checks:

1. Be a 302 response.
2. Have a *location* header with a valid, fully qualified URL matching the scheme of the original request.
3. The request must have been fully processed by Envoy.
4. The request must not have a body.
5. The number of previously handled internal redirect within a given downstream request does not exceed
   `max internal redirects <envoy_api_field_route.RouteAction.max_internal_redirects>` of the route
   that the request or redirected request is hitting.

Any failure will result in redirect being passed downstream instead.

Since a redirected request may be bounced between different routes, any route in the chain of redirects that

1. does not have internal redirect enabled
2. or has a `max internal redirects
   <envoy_api_field_route.RouteAction.max_internal_redirects>`
   smaller or equal to the redirect chain length when the redirect chain hits it

will cause the redirect to be passed downstream.

Once the redirect has passed these checks, the request headers which were shipped to the original
upstream will be modified by:

1. Putting the fully qualified original request URL in the x-envoy-original-url header.
2. Replacing the Authority/Host, Scheme, and Path headers with the values from the Location header.

The altered request headers will then have a new route selected, be sent through a new filter chain,
and then shipped upstream with all of the normal Envoy request sanitization taking place.

.. warning::
  Note that HTTP connection manager sanitization such as clearing untrusted headers will only be
  applied once. Per-route header modifications will be applied on both the original route and the
  second route, even if they are the same, so be careful configuring header modification rules to
  avoid duplicating undesired header values.

A sample redirect flow might look like this:

1. Client sends a GET request for *\http://foo.com/bar*
2. Upstream 1 sends a 302 with  *"location: \http://baz.com/eep"*
3. Envoy is configured to allow redirects on the original route, and sends a new GET request to
   Upstream 2, to fetch *\http://baz.com/eep* with the additional request header
   *"x-envoy-original-url: \http://foo.com/bar"*
4. Envoy proxies the response data for *\http://baz.com/eep* to the client as the response to the original
   request.


Timeouts
--------

Various configurable timeouts apply to an HTTP connection and its constituent streams. Please see
:ref:`this FAQ entry <faq_configuration_timeouts>` for an overview of important timeout
configuration.
