.. _arch_overview_http_conn_man:

HTTP connection management
==========================

HTTP is such a critical component of modern service oriented architectures that Envoy implements a
large amount of HTTP specific functionality. Envoy has a built in network level filter called the
:ref:`HTTP connection manager <config_http_conn_man>`.

This filter translates raw bytes into HTTP level messages and events (e.g., headers received,
body data received, trailers received, etc.).

It also handles functionality common to all HTTP connections and requests such as
:ref:`access logging <arch_overview_access_logs>`, :ref:`request ID generation and tracing <arch_overview_tracing>`,
:ref:`request/response header manipulation <config_http_conn_man_headers>`, :ref:`route table
<arch_overview_http_routing>` management, and :ref:`statistics <config_http_conn_man_stats>`.

.. tip::
   See the HTTP connection manager :ref:`configuration <config_http_conn_man>` and
   :ref:`protobuf <envoy_v3_api_file_envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.proto>`
   sections for reference documentation.

.. _arch_overview_http_protocols:

HTTP protocols
--------------

Envoy’s HTTP connection manager has native support for **HTTP/1.1**, **HTTP/2** and **HTTP/3**, including **WebSockets**.

Envoy’s HTTP support was designed to first and foremost be an HTTP/2 multiplexing proxy. Internally, HTTP/2 terminology
is used to describe system components. For example, an HTTP request and response take place on a "stream".

A codec API is used to translate from different wire protocols into a protocol agnostic form for streams, requests, responses, etc.

In the case of HTTP/1.1, the codec translates the serial/pipelining capabilities of the protocol into something
that looks like HTTP/2 to higher layers. This means that the majority of the code does not need to
understand whether a stream originated on an HTTP/1.1, HTTP/2, or HTTP/3 connection.

HTTP lifecycle
--------------

Proxying of the request begins when the downstream HTTP codec has successfully decoded request header map.

The point at which the proxying completes and the stream is destroyed depends on the upstream protocol and
whether independent half close is enabled.

If independent half-close is enabled and the upstream protocol is either HTTP/2 or HTTP/3 protocols the stream
is destroyed after both request and response are complete i.e. reach their respective end-of-stream,
by receiving trailers or the header/body with end-stream set in both directions AND response has
success (2xx) status code.

For HTTP/1 upstream protocol or if independent half-close is disabled the stream is destroyed when the response
is complete and reaches its end-of-stream, i.e. when trailers or the response header/body with
end-stream set are received, even if the request has not yet completed. If the request was incomplete at response
completion, the stream is reset.

Note that proxying can stop early when an error or timeout occurred or when a peer reset HTTP/2 or HTTP/3 stream.

HTTP header sanitizing
----------------------

The HTTP connection manager performs various :ref:`header sanitizing
<config_http_conn_man_header_sanitizing>` actions for security reasons.

Route table configuration
-------------------------

Each :ref:`HTTP connection manager filter <config_http_conn_man>` has an associated :ref:`route
table <arch_overview_http_routing>`, which can be specified in one of two ways:

* Statically.
* Dynamically via the :ref:`RDS API <config_http_conn_man_rds>`.

.. _arch_overview_http_retry_plugins:

Retry plugin configuration
--------------------------

Normally during retries, host selection follows the same process as the original request. Retry plugins
can be used to modify this behavior, and they fall into two categories:

:ref:`Host Predicates <envoy_v3_api_field_config.route.v3.RetryPolicy.retry_host_predicate>`
     These predicates can be used to "reject" a host, which will cause host selection to be reattempted.
     Any number of these predicates can be specified, and **the host will be rejected if any of the predicates reject the host**.

     Envoy supports the following built-in host predicates:

     :ref:`PreviousHostsPredicate <envoy_v3_api_file_envoy/extensions/retry/host/previous_hosts/v3/previous_hosts.proto>`
          This will keep track of previously attempted hosts, and rejects hosts that have already been attempted.

     :ref:`OmitCanaryHostsPredicate <envoy_v3_api_file_envoy/extensions/retry/host/omit_canary_hosts/v3/omit_canary_hosts.proto>`
          This will reject any host that is a marked as canary host.
          Hosts are marked by setting ``canary: true`` for the ``envoy.lb`` filter in the endpoint's filter metadata.
          See :ref:`LbEndpoint <envoy_v3_api_msg_config.endpoint.v3.LbEndpoint>` for more details.

     :ref:`OmitHostMetadataConfig <envoy_v3_api_file_envoy/extensions/retry/host/omit_host_metadata/v3/omit_host_metadata_config.proto>`
          This will reject any host based on predefined metadata match criteria.
          See the configuration example below for more details.

:ref:`Priority Predicates<envoy_v3_api_field_config.route.v3.RetryPolicy.retry_priority>`
     These predicates can
     be used to adjust the priority load used when selecting a priority for a retry attempt. **Only one priority
     predicate may be specified**.

     Envoy supports the following built-in priority predicates:

     :ref:`PreviousPrioritiesConfig <envoy_v3_api_file_envoy/extensions/retry/priority/previous_priorities/v3/previous_priorities_config.proto>`
          This will keep track of previously attempted priorities, and adjust the priority
          load such that other priorities will be targeted in subsequent retry attempts.

Host selection will continue until either the configured predicates accept the host or a configurable
:ref:`max attempts <envoy_v3_api_field_config.route.v3.RetryPolicy.host_selection_retry_max_attempts>` has been reached.

These plugins can be combined to affect both host selection and priority load. Envoy can also be extended
with custom retry plugins similar to how custom filters can be added.


Retry configuration examples
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``PreviousHostsPredicate``
++++++++++++++++++++++++++

For example, to configure retries to prefer hosts that haven't been attempted already, the built-in
:ref:`PreviousHostsPredicate <envoy_v3_api_file_envoy/extensions/retry/host/previous_hosts/v3/previous_hosts.proto>`
can be used:

.. literalinclude:: /_configs/retry/retry-previous-hosts.yaml
    :language: yaml
    :lines: 24-36
    :emphasize-lines: 6-11
    :linenos:
    :lineno-start: 24
    :caption: :download:`retry-previous-hosts.yaml </_configs/retry/retry-previous-hosts.yaml>`

This will reject hosts previously attempted, retrying host selection a maximum of 3 times. The bound
on attempts is necessary in order to deal with scenarios in which finding an acceptable host is either
impossible (no hosts satisfy the predicate) or very unlikely (the only suitable host has a very low
relative weight).

``OmitHostMetadataConfig``
++++++++++++++++++++++++++

To reject a host based on its metadata,
:ref:`OmitHostMetadataConfig <envoy_v3_api_file_envoy/extensions/retry/host/omit_host_metadata/v3/omit_host_metadata_config.proto>`
can be used:

.. literalinclude:: /_configs/retry/retry-omit-host-metadata.yaml
    :language: yaml
    :lines: 24-39
    :emphasize-lines: 6-14
    :linenos:
    :lineno-start: 24
    :caption: :download:`retry-omit-host-metadata.yaml </_configs/retry/retry-omit-host-metadata.yaml>`

This will reject any host with matching (key, value) in its metadata.

``PreviousPrioritiesConfig``
++++++++++++++++++++++++++++

To configure retries to attempt other priorities during retries, the built-in
:ref:`PreviousPrioritiesConfig <envoy_v3_api_file_envoy/extensions/retry/priority/previous_priorities/v3/previous_priorities_config.proto>`
can be used.

.. literalinclude:: /_configs/retry/retry-previous-priorities.yaml
    :language: yaml
    :lines: 24-36
    :emphasize-lines: 6-11
    :linenos:
    :lineno-start: 24
    :caption: :download:`retry-previous-priorities.yaml </_configs/retry/retry-previous-priorities.yaml>`

This will target priorities in subsequent retry attempts that haven't been already used. The
:ref:`update_frequency <envoy_v3_api_field_extensions.retry.priority.previous_priorities.v3.PreviousPrioritiesConfig.update_frequency>`
parameter decides how often the priority load should be recalculated.

Combined retry policy
+++++++++++++++++++++

These plugins can be combined, which will exclude both previously attempted hosts as well as
previously attempted priorities.

.. literalinclude:: /_configs/retry/retry-combined-hosts-priorities.yaml
    :language: yaml
    :lines: 24-41
    :emphasize-lines: 6-16
    :linenos:
    :lineno-start: 24
    :caption: :download:`retry-combined-hosts-priorities.yaml </_configs/retry/retry-combined-hosts-priorities.yaml>`

.. _arch_overview_internal_redirects:

Internal redirects
------------------

Envoy supports handling 3xx redirects internally, that is capturing a configurable 3xx redirect
response, synthesizing a new request, sending it to the upstream specified by the new route match,
and returning the redirected response as the response to the original request. The headers and body
of the original request will be sent in the redirect to the new location. Trailers are not yet
supported.

Internal redirects are configured via the :ref:`internal_redirect_policy
<envoy_v3_api_field_config.route.v3.RouteAction.internal_redirect_policy>` field in route configuration.
When redirect handling is on, any 3xx response from upstream, that matches
:ref:`redirect_response_codes
<envoy_v3_api_field_config.route.v3.InternalRedirectPolicy.redirect_response_codes>`
is subject to the redirect being handled by Envoy.

If Envoy is configured to internally redirect HTTP ``303`` and receives an HTTP ``303`` response, it will
dispatch the redirect with a bodiless HTTP ``GET`` if the original request was not a ``GET`` or ``HEAD``
request. Otherwise, Envoy will preserve the original HTTP method. For more information, see `RFC
7231 Section 6.4.4 <https://tools.ietf.org/html/rfc7231#section-6.4.4>`_.

For a redirect to be handled successfully it **must** pass the following checks:

#. Have a response code matching one of :ref:`redirect_response_codes
   <envoy_v3_api_field_config.route.v3.InternalRedirectPolicy.redirect_response_codes>`, which is
   either ``302`` (by default), or a set of 3xx codes (``301``, ``302``, ``303``, ``307``, ``308``).
#. Have a ``location`` header with a valid, fully qualified URL.
#. The request must have been fully processed by Envoy.
#. The request must be smaller than the :ref:`per_request_buffer_limit_bytes
   <envoy_v3_api_field_config.route.v3.Route.per_request_buffer_limit_bytes>` limit.
#. :ref:`allow_cross_scheme_redirect
   <envoy_v3_api_field_config.route.v3.InternalRedirectPolicy.allow_cross_scheme_redirect>` is ``true`` (default to ``false``),
   or the scheme of the downstream request and the ``location`` header are the same.
#. The number of previously handled internal redirect within a given downstream request does not
   exceed :ref:`max_internal_redirects
   <envoy_v3_api_field_config.route.v3.InternalRedirectPolicy.max_internal_redirects>`
   of the route that the request or redirected request is hitting.
#. All :ref:`predicates <envoy_v3_api_field_config.route.v3.InternalRedirectPolicy.predicates>` accept
   the target route.

Any failure will result in the redirect being passed downstream instead.

Since a redirected request may be bounced between different routes, any route in the chain of redirects that ...

* does not have internal redirect enabled
* *or,* has a :ref:`max_internal_redirects
  <envoy_v3_api_field_config.route.v3.InternalRedirectPolicy.max_internal_redirects>`
  smaller or equal to the redirect chain length when the redirect chain hits it
* *or,* is disallowed by any of the :ref:`predicates
  <envoy_v3_api_field_config.route.v3.InternalRedirectPolicy.predicates>`

\... will cause the redirect to be passed downstream.

Two predicates can be used to create a DAG that defines the redirect chain, the :ref:`previous_routes
<envoy_v3_api_msg_extensions.internal_redirect.previous_routes.v3.PreviousRoutesConfig>` predicate, and
the :ref:`allow_listed_routes
<envoy_v3_api_msg_extensions.internal_redirect.allow_listed_routes.v3.AllowListedRoutesConfig>`.
Specifically, the :ref:`allow_listed_routes
<envoy_v3_api_msg_extensions.internal_redirect.allow_listed_routes.v3.AllowListedRoutesConfig>` predicate
defines edges of individual node in the DAG and the :ref:`previous_routes
<envoy_v3_api_msg_extensions.internal_redirect.previous_routes.v3.PreviousRoutesConfig>` predicate defines
"visited" state of the edges, so that loop can be avoided if so desired.

A third predicate :ref:`safe_cross_scheme
<envoy_v3_api_msg_extensions.internal_redirect.safe_cross_scheme.v3.SafeCrossSchemeConfig>`
can be used to prevent HTTP -> HTTPS redirect.

Once the redirect has passed these checks, the request headers which were shipped to the original
upstream will be modified by:

#. Putting the fully qualified original request URL in the ``x-envoy-original-url`` header.
#. Replacing the ``Authority``/``Host``, ``Scheme``, and ``Path`` headers with the values from the ``Location`` header.
#. Copying any headers listed in
   :ref:`response_headers_to_copy<envoy_v3_api_field_config.route.v3.InternalRedirectPolicy.response_headers_to_copy>`
   from the redirect response into the headers that will be used in the
   subsequent request.

The altered request headers will then have a new route selected, be sent through a new filter chain,
and then shipped upstream with all of the normal Envoy request sanitization taking place.

.. warning::
  Note that HTTP connection manager sanitization such as clearing untrusted headers will only be
  applied once. Per-route header modifications will be applied on both the original route and the
  second route, even if they are the same, so be careful configuring header modification rules to
  avoid duplicating undesired header values.

.. warning::
  Note that no downstream filters will see the response that triggers the internal redirect. If there is a need
  to pass data between the redirect response and the followup request, see
  :ref:`response_headers_to_copy<envoy_v3_api_field_config.route.v3.InternalRedirectPolicy.response_headers_to_copy>`.

A sample redirect flow might look like this:

#. Client sends a ``GET`` request for http://foo.com/bar.
#. Upstream 1 sends a ``302`` with  ``location: http://baz.com/eep``.
#. Envoy is configured to allow redirects on the original route, and sends a new ``GET`` request to
   Upstream 2, to fetch http://baz.com/eep with the additional request header
   ``x-envoy-original-url: http://foo.com/bar``.
#. Envoy proxies the response data for http://baz.com/eep to the client as the response to the original
   request.

Timeouts
--------

Various configurable timeouts apply to an HTTP connection and its constituent streams. Please see
:ref:`this FAQ entry <faq_configuration_timeouts>` for an overview of important timeout
configuration.

.. _arch_overview_http_header_map_settings:

HTTP header map settings
------------------------

Envoy maintains the insertion order of headers (and pseudo headers that begin with ``:``) in the
HTTP header map using a linked list data-structure, which is very fast when the number of headers
is small.
