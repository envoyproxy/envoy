.. _config_http_conn_man_headers:

HTTP header manipulation
========================

The HTTP connection manager manipulates several HTTP headers both during decoding (when the request
is being received) as well as during encoding (when the response is being sent).

.. contents::
  :local:

.. _config_http_conn_man_headers_scheme:

:scheme
-------

Envoy will always set the ``:scheme`` header while processing a request. It should always be available to filters, and should be forwarded upstream for HTTP/2 and HTTP/3, where :ref:`config_http_conn_man_headers_x-forwarded-proto` will be sent for HTTP/1.1.

For HTTP/2, and HTTP/3, incoming ``:scheme`` headers are trusted and propogated through upstream.
For HTTP/1, the ``:scheme`` header will be set
1) From the absolute URL if present and valid. An invalid (not "http" or "https") scheme, or an https scheme over an unencrypted connection will result in Envoy rejecting the request. This is the only scheme validation Envoy performs as it avoids a HTTP/1.1-specific privilege escalation attack for edge Envoys [1]_ which doesn't have a comparable vector for HTTP/2 and above [2]_.
2) From the value of the :ref:`config_http_conn_man_headers_x-forwarded-proto` header after sanitization (to valid ``x-forwarded-proto`` from trusted downstreams, otherwise based on downstream encryption level).

This default behavior can be overridden via the :ref:`scheme_header_transformation
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.scheme_header_transformation>`
configuration option.

The ``:scheme`` header will be used by Envoy over ``x-forwarded-proto`` where the URI scheme is wanted, for example serving content from cache based on the ``:scheme`` header rather than X-Forwarded-Proto, or setting the scheme of redirects based on the scheme of the original URI. See :ref:`why_is_envoy_using_xfp_or_scheme` for more details.

.. [1] Edge Envoys often have plaintext HTTP/1.1 listeners. If Envoy trusts absolute URL scheme from fully qualfied URLs, a MiTM can adjust relative URLs to https absolute URLs, and inadvertently cause the Envoy's upstream to send PII or other sensitive data over what it then believes is a secure connection.
.. [2] Unlike HTTP/1.1, HTTP/2 is in practice always served over TLS via ALPN for edge Envoys. In mesh networks using insecure HTTP/2, if the downstream is not trusted to set scheme, the :ref:`scheme_header_transformation <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.scheme_header_transformation>` should be used.

.. _config_http_conn_man_headers_user-agent:

:path
-----

The ``:path`` header is a pseudo-header populated by Envoy using the value of the path of the HTTP
request. E.g. an HTTP request of the form ``GET /docs/thing HTTP/1.1`` would have a ``:path`` value
of ``/docs/thing``.

:method
-------

The ``:method`` header is a pseudo-header populated by Envoy using the value of the method of the HTTP
request. E.g. an HTTP request of the form ``GET /docs/thing HTTP/1.1`` would have a ``:method`` value
of ``GET``. Values are in upper-case, e.g. ``GET``, ``PUT``, ``POST``, and ``DELETE``. Other possible
values are described in `HTTP Methods <https://developer.mozilla.org/en-US/docs/Web/HTTP/Methods>`.

user-agent
----------

The ``user-agent`` header may be set by the connection manager during decoding if the :ref:`add_user_agent
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.add_user_agent>` option is
enabled. The header is only modified if it is not already set. If the connection manager does set the header, the value
is determined by the :option:`--service-cluster` command line option.

.. _config_http_conn_man_headers_server:

server
------

The ``server`` header will be set during encoding to the value in the :ref:`server_name
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.server_name>` option.

.. _config_http_conn_man_headers_referer:

referer
-------

The ``referer`` header will be sanitized during decoding. Multiple URLs, invalid relative URLs
containing a fragment component, and valid absolute URLs containing userinfo or a fragment component
will be removed.

.. _config_http_conn_man_headers_x-client-trace-id:

x-client-trace-id
-----------------

If an external client sets this header, Envoy will join the provided trace ID with the internally
generated :ref:`config_http_conn_man_headers_x-request-id`. x-client-trace-id needs to be globally
unique and generating a uuid4 is recommended. If this header is set, it has similar effect to
:ref:`config_http_conn_man_headers_x-envoy-force-trace`. See the :ref:`tracing.client_enabled
<config_http_conn_man_runtime_client_enabled>` runtime configuration setting.

.. _config_http_conn_man_headers_downstream-service-cluster:

x-envoy-downstream-service-cluster
----------------------------------

Internal services often want to know which service is calling them. This header is cleaned from
external requests, but for internal requests will contain the service cluster of the caller. Note
that in the current implementation, this should be considered a hint as it is set by the caller and
could be easily spoofed by any internal entity. In the future Envoy will support a mutual
authentication TLS mesh which will make this header fully secure. Like ``user-agent``, the value
is determined by the :option:`--service-cluster` command line option. In order to enable this
feature you need to set the :ref:`user_agent <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.add_user_agent>` option to true.

.. _config_http_conn_man_headers_downstream-service-node:

x-envoy-downstream-service-node
-------------------------------

Internal services may want to know the downstream node request comes from. This header
is quite similar to :ref:`config_http_conn_man_headers_downstream-service-cluster`, except the value is taken from
the  :option:`--service-node` option.

.. _config_http_conn_man_headers_x-envoy-external-address:

x-envoy-external-address
------------------------

It is a common case where a service wants to perform analytics based on the origin client's IP
address. Per the lengthy discussion on :ref:`XFF <config_http_conn_man_headers_x-forwarded-for>`,
this can get quite complicated, so Envoy simplifies this by setting ``x-envoy-external-address``
to the :ref:`trusted client address <config_http_conn_man_headers_x-forwarded-for_trusted_client_address>`
if the request is from an external client. ``x-envoy-external-address`` is not set or overwritten
for internal requests. This header can be safely forwarded between internal services for analytics
purposes without having to deal with the complexities of XFF.

.. _config_http_conn_man_headers_x-envoy-force-trace:

x-envoy-force-trace
-------------------

If an internal request sets this header, Envoy will modify the generated
:ref:`config_http_conn_man_headers_x-request-id` such that it forces traces to be collected.
This also forces :ref:`config_http_conn_man_headers_x-request-id` to be returned in the response
headers. If this request ID is then propagated to other hosts, traces will also be collected on
those hosts which will provide a consistent trace for an entire request flow. See the
:ref:`tracing.global_enabled <config_http_conn_man_runtime_global_enabled>` and
:ref:`tracing.random_sampling <config_http_conn_man_runtime_random_sampling>` runtime
configuration settings.

.. _config_http_conn_man_headers_x-envoy-internal:

x-envoy-internal
----------------

It is a common case where a service wants to know whether a request is internal origin or not. Envoy
uses :ref:`XFF <config_http_conn_man_headers_x-forwarded-for>` to determine this and then will set
the header value to ``true``.

This is a convenience to avoid having to parse and understand XFF.

.. _config_http_conn_man_headers_x-envoy-original-dst-host:

x-envoy-original-dst-host
-------------------------

The header used to override destination address when using the
:ref:`Original Destination <arch_overview_load_balancing_types_original_destination>`
load balancing policy.

It is ignored, unless the use of it is enabled via
:ref:`use_http_header <envoy_v3_api_field_config.cluster.v3.Cluster.OriginalDstLbConfig.use_http_header>`.

.. _config_http_conn_man_headers_x-forwarded-client-cert:

x-forwarded-client-cert
-----------------------

``x-forwarded-client-cert`` (XFCC) is a proxy header which indicates certificate information of part
or all of the clients or proxies that a request has flowed through, on its way from the client to the
server. A proxy may choose to sanitize/append/forward the XFCC header before proxying the request.

The XFCC header value is a comma (",") separated string. Each substring is an XFCC element, which
holds information added by a single proxy. A proxy can append the current client certificate
information as an XFCC element, to the end of the request's XFCC header after a comma.

Each XFCC element is a semicolon ";" separated string. Each substring is a key-value pair, grouped
together by an equals ("=") sign. The keys are case-insensitive, the values are case-sensitive. If
",", ";" or "=" appear in a value, the value should be double-quoted. Double-quotes in the value
should be replaced by backslash-double-quote (\").

The following keys are supported:

1. ``By`` The Subject Alternative Name (URI type) of the current proxy's certificate. The current proxy's certificate may contain multiple URI type Subject Alternative Names, each will be a separate key-value pair.
2. ``Hash`` The SHA 256 digest of the current client certificate.
3. ``Cert`` The entire client certificate in URL encoded PEM format.
4. ``Chain`` The entire client certificate chain (including the leaf certificate) in URL encoded PEM format.
5. ``Subject`` The Subject field of the current client certificate. The value is always double-quoted.
6. ``URI`` The URI type Subject Alternative Name field of the current client certificate. A client certificate may contain multiple URI type Subject Alternative Names, each will be a separate key-value pair.
7. ``DNS`` The DNS type Subject Alternative Name field of the current client certificate. A client certificate may contain multiple DNS type Subject Alternative Names, each will be a separate key-value pair.

A client certificate may contain multiple Subject Alternative Name types. For details on different Subject Alternative Name types, please refer `RFC 2459`_.

.. _RFC 2459: https://tools.ietf.org/html/rfc2459#section-4.2.1.7

Some examples of the XFCC header are:

1. For one client certificate with only URI type Subject Alternative Name: ``x-forwarded-client-cert: By=http://frontend.lyft.com;Hash=468ed33be74eee6556d90c0149c1309e9ba61d6425303443c0748a02dd8de688;Subject="/C=US/ST=CA/L=San Francisco/OU=Lyft/CN=Test Client";URI=http://testclient.lyft.com``
2. For two client certificates with only URI type Subject Alternative Name: ``x-forwarded-client-cert: By=http://frontend.lyft.com;Hash=468ed33be74eee6556d90c0149c1309e9ba61d6425303443c0748a02dd8de688;URI=http://testclient.lyft.com,By=http://backend.lyft.com;Hash=9ba61d6425303443c0748a02dd8de688468ed33be74eee6556d90c0149c1309e;URI=http://frontend.lyft.com``
3. For one client certificate with both URI type and DNS type Subject Alternative Name: ``x-forwarded-client-cert: By=http://frontend.lyft.com;Hash=468ed33be74eee6556d90c0149c1309e9ba61d6425303443c0748a02dd8de688;Subject="/C=US/ST=CA/L=San Francisco/OU=Lyft/CN=Test Client";URI=http://testclient.lyft.com;DNS=lyft.com;DNS=www.lyft.com``

How Envoy processes XFCC is specified by the
:ref:`forward_client_cert_details<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.forward_client_cert_details>`
and the
:ref:`set_current_client_cert_details<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.set_current_client_cert_details>`
HTTP connection manager options. If ``forward_client_cert_details`` is unset, the XFCC header will be sanitized by
default.

.. _config_http_conn_man_headers_x-forwarded-for:

x-forwarded-for
---------------

``x-forwarded-for`` (XFF) is a standard proxy header which indicates the IP addresses that a request has
flowed through on its way from the client to the server. A compliant proxy will ``append`` the IP
address of the nearest client to the XFF list before proxying the request. Some examples of XFF are:

1. ``x-forwarded-for: 50.0.0.1`` (single client)
2. ``x-forwarded-for: 50.0.0.1, 40.0.0.1`` (external proxy hop)
3. ``x-forwarded-for: 50.0.0.1, 10.0.0.1`` (internal proxy hop)

Envoy will only append to XFF if the :ref:`use_remote_address
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.use_remote_address>`
HTTP connection manager option is set to true and the :ref:`skip_xff_append
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.skip_xff_append>`
is set false. This means that if ``use_remote_address`` is false (which is the default) or
``skip_xff_append`` is true, the connection manager operates in a transparent mode where it does not
modify XFF.

.. attention::

  In general, ``use_remote_address`` should be set to true when Envoy is deployed as an edge
  node (aka a front proxy), whereas it may need to be set to false when Envoy is used as
  an internal service node in a mesh deployment.

.. _config_http_conn_man_headers_x-forwarded-for_trusted_client_address:

The value of ``use_remote_address`` controls how Envoy determines the *trusted client address*.
Given an HTTP request that has traveled through a series of zero or more proxies to reach
Envoy, the trusted client address is the earliest source IP address that is known to be
accurate. The source IP address of the immediate downstream node's connection to Envoy is
trusted. XFF *sometimes* can be trusted. Malicious clients can forge XFF, but the last
address in XFF can be trusted if it was put there by a trusted proxy.

Alternatively, Envoy supports
:ref:`extensions <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.original_ip_detection_extensions>`
for determining the *trusted client address* or original IP address.

.. note::

 The use of such extensions cannot be mixed with ``use_remote_address`` nor ``xff_num_trusted_hops``.

Envoy's default rules for determining the trusted client address (*before* appending anything
to XFF) are:

* If ``use_remote_address`` is false and an XFF containing at least one IP address is
  present in the request, the trusted client address is the *last* (rightmost) IP address in XFF.
* Otherwise, the trusted client address is the source IP address of the immediate downstream
  node's connection to Envoy.

In an environment where there are one or more trusted proxies in front of an edge
Envoy instance, the ``xff_num_trusted_hops`` configuration option can be used to trust
additional addresses from XFF:

* If ``use_remote_address`` is false and ``xff_num_trusted_hops`` is set to a value *N* that is
  greater than zero, the trusted client address is the (N+1)th address from the right end of
  XFF. (If the XFF contains fewer than N+1 addresses, Envoy falls back to using the immediate
  downstream connection's source address as trusted client address.)
* If ``use_remote_address`` is true and ``xff_num_trusted_hops`` is set to a value *N* that is
  greater than zero, the trusted client address is the Nth address from the right end of XFF. (If
  the XFF contains fewer than N addresses, Envoy falls back to using the immediate downstream
  connection's source address as trusted client address.)

.. note::

 If the trusted client address should be determined from a list of known CIDRs, use the
 :ref:`xff <envoy_v3_api_msg_extensions.http.original_ip_detection.xff.v3.XffConfig>` original IP
 detection option instead.

 * If the remote address is contained by an entry in ``xff_trusted_cidrs``, and the *last*
   (rightmost) entry is also contained by an entry in ``xff_trusted_cidrs``, the trusted client
   address is *second-last* IP address in XFF.
 * If all entries in XFF are contained by an entry in ``xff_trusted_cidrs``, the trusted client
   address is the *first* (leftmost) IP address in XFF.

Envoy uses the trusted client address contents to determine whether a request originated
externally or internally. This influences whether the
:ref:`config_http_conn_man_headers_x-envoy-internal` header is set.

Example 1: Envoy as edge proxy, without a trusted proxy in front of it
    Settings:
      | use_remote_address = true
      | xff_num_trusted_hops = 0

    Request details:
      | Downstream IP address = 192.0.2.5
      | XFF = "203.0.113.128, 203.0.113.10, 203.0.113.1"

    Result:
      | Trusted client address = 192.0.2.5 (XFF is ignored)
      | X-Envoy-External-Address is set to 192.0.2.5
      | XFF is changed to "203.0.113.128, 203.0.113.10, 203.0.113.1, 192.0.2.5"
      | X-Envoy-Internal is removed (if it was present in the incoming request)

Example 2: Envoy as internal proxy, with the Envoy edge proxy from Example 1 in front of it
    Settings:
      | use_remote_address = false
      | xff_num_trusted_hops = 0

    Request details:
      | Downstream IP address = 10.11.12.13 (address of the Envoy edge proxy)
      | XFF = "203.0.113.128, 203.0.113.10, 203.0.113.1, 192.0.2.5"

    Result:
      | Trusted client address = 192.0.2.5 (last address in XFF is trusted)
      | X-Envoy-External-Address is not modified
      | X-Envoy-Internal is removed (if it was present in the incoming request)

Example 3: Envoy as edge proxy, with two trusted external proxies in front of it
    Settings:
      | use_remote_address = true
      | xff_num_trusted_hops = 2

    Request details:
      | Downstream IP address = 192.0.2.5
      | XFF = "203.0.113.128, 203.0.113.10, 203.0.113.1"

    Result:
      | Trusted client address = 203.0.113.10 (2nd to last address in XFF is trusted)
      | X-Envoy-External-Address is set to 203.0.113.10
      | XFF is changed to "203.0.113.128, 203.0.113.10, 203.0.113.1, 192.0.2.5"
      | X-Envoy-Internal is removed (if it was present in the incoming request)

Example 4: Envoy as internal proxy, with the edge proxy from Example 3 in front of it
    Settings:
      | use_remote_address = false
      | xff_num_trusted_hops = 2

    Request details:
      | Downstream IP address = 10.11.12.13 (address of the Envoy edge proxy)
      | XFF = "203.0.113.128, 203.0.113.10, 203.0.113.1, 192.0.2.5"

    Result:
      | Trusted client address = 203.0.113.10
      | X-Envoy-External-Address is not modified
      | X-Envoy-Internal is removed (if it was present in the incoming request)

Example 5: Envoy as an internal proxy, receiving a request from an internal client
    Settings:
      | use_remote_address = false
      | xff_num_trusted_hops = 0

    Request details:
      | Downstream IP address = 10.20.30.40 (address of the internal client)
      | XFF is not present

    Result:
      | Trusted client address = 10.20.30.40
      | X-Envoy-External-Address remains unset
      | X-Envoy-Internal is set to "false"

Example 6: The internal Envoy from Example 5, receiving a request proxied by another Envoy
    Settings:
      | use_remote_address = false
      | xff_num_trusted_hops = 0

    Request details:
      | Downstream IP address = 10.20.30.50 (address of the Envoy instance proxying to this one)
      | XFF = "10.20.30.40"

    Result:
      | Trusted client address = 10.20.30.40
      | X-Envoy-External-Address remains unset
      | X-Envoy-Internal is set to "true"

Example 7: Envoy as edge proxy, with one trusted CIDR
    Settings:
      | use_remote_address = false
      | xff_trusted_cidrs = 192.0.2.0/24

    Request details:
      | Downstream IP address = 192.0.2.5
      | XFF = "203.0.113.128, 203.0.113.10, 192.0.2.1"

    Result:
      | Trusted client address = 192.0.2.1
      | X-Envoy-External-Address is set to 192.0.2.1
      | XFF is changed to "203.0.113.128, 203.0.113.10, 192.0.2.1, 192.0.2.5"
      | X-Envoy-Internal is removed (if it was present in the incoming request)

Example 8: Envoy as edge proxy, with two trusted CIDRs
    Settings:
      | use_remote_address = false
      | xff_trusted_cidrs = 192.0.2.0/24, 198.51.100.0/24

    Request details:
      | Downstream IP address = 192.0.2.5
      | XFF = "203.0.113.128, 203.0.113.10, 198.51.100.1"

    Result:
      | Trusted client address = 203.0.113.10
      | X-Envoy-External-Address is set to 203.0.113.10
      | XFF is changed to "203.0.113.128, 203.0.113.10, 198.51.100.1, 192.0.2.5"
      | X-Envoy-Internal is removed (if it was present in the incoming request)

A few very important notes about XFF:

1. If ``use_remote_address`` is set to true, Envoy sets the
   :ref:`config_http_conn_man_headers_x-envoy-external-address` header to the trusted
   client address.

.. _config_http_conn_man_headers_x-forwarded-for_internal_origin:

2. XFF is what Envoy uses to determine whether a request is internal origin or external origin.
   If ``use_remote_address`` is set to true, the request is internal if and only if the
   request contains no XFF and the immediate downstream node's connection to Envoy has
   an internal (RFC1918 or RFC4193) source address. If ``use_remote_address`` is false, the
   request is internal if and only if XFF contains a single RFC1918 or RFC4193 address.

   * **NOTE**: If an internal service proxies an external request to another internal service, and
     includes the original XFF header, Envoy will append to it on egress if
     :ref:`use_remote_address <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.use_remote_address>` is set. This will cause
     the other side to think the request is external. Generally, this is what is intended if XFF is
     being forwarded. If it is not intended, do not forward XFF, and forward
     :ref:`config_http_conn_man_headers_x-envoy-internal` instead.
   * **NOTE**: If an internal service call is forwarded to another internal service (preserving XFF),
     Envoy will not consider it internal. This is a known "bug" due to the simplification of how
     XFF is parsed to determine if a request is internal. In this scenario, do not forward XFF and
     allow Envoy to generate a new one with a single internal origin IP.

.. _config_http_conn_man_headers_x-forwarded-host:

x-forwarded-host
----------------

The ``x-forwarded-host`` header is a de-facto standard proxy header which indicates the original
host requested by the client in the ``:authority`` (``host`` in HTTP1) header. A compliant proxy
*appends* the original value of the ``:authority`` header to ``x-forwarded-host`` only if the
``:authority`` header is modified.

Envoy updates the ``:authority`` header if a host rewrite option (one of
:ref:`host_rewrite_literal <envoy_v3_api_field_config.route.v3.RouteAction.host_rewrite_literal>`,
:ref:`auto_host_rewrite <envoy_v3_api_field_config.route.v3.RouteAction.auto_host_rewrite>`,
:ref:`host_rewrite_header <envoy_v3_api_field_config.route.v3.RouteAction.host_rewrite_header>`, or
:ref:`host_rewrite_path_regex <envoy_v3_api_field_config.route.v3.RouteAction.host_rewrite_path_regex>`)
is used and appends its original value to ``x-forwarded-host`` if
:ref:`append_x_forwarded_host <envoy_v3_api_field_config.route.v3.RouteAction.append_x_forwarded_host>`
is set.

.. _config_http_conn_man_headers_x-forwarded-port:

x-forwarded-port
----------------

Usually, the ``x-forwarded-port`` header comes with the ``x-forwarded-proto`` header for service to
know the originating destination port of the connection, which is the listener port in Envoy's side.

Envoy will append the ``x-forwarded-port`` header if :ref:`append_x_forwarded_port
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.append_x_forwarded_port>`
is set to true and the header has not been set.

Downstream ``x-forwarded-port`` headers will only be trusted if ``xff_num_trusted_hops`` is non-zero.
If ``xff_num_trusted_hops`` is zero, downstream ``x-forwarded-port`` headers will be overwritten.

.. _config_http_conn_man_headers_x-forwarded-proto:

x-forwarded-proto
-----------------

It is a common case where a service wants to know what the originating protocol (HTTP or HTTPS) was
of the connection terminated by front/edge Envoy. ``x-forwarded-proto`` contains this information. It
will be set to either ``http`` or ``https``.

Downstream ``x-forwarded-proto`` headers will only be trusted if ``xff_num_trusted_hops`` is non-zero.
If ``xff_num_trusted_hops`` is zero, downstream ``x-forwarded-proto`` headers and ``:scheme`` headers
will be set to http or https based on if the downstream connection is TLS or not.

If the scheme is changed via the :ref:`scheme_header_transformation
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.scheme_header_transformation>`
configuration option, ``x-forwarded-proto`` will be updated as well.

The ``x-forwarded-proto`` header will be used by Envoy over ``:scheme`` where the underlying
encryption is wanted, for example clearing default ports based on ``x-forwarded-proto``. See
:ref:`why_is_envoy_using_xfp_or_scheme` for more details.

.. _config_http_conn_man_headers_x-envoy-local-overloaded:

x-envoy-local-overloaded
------------------------

Envoy will set this header on the downstream response
if a request was dropped due to :ref:`overload manager<arch_overview_overload_manager>` and
:ref:`configuration option <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.append_local_overload>`
is set to true.

.. _config_http_conn_man_headers_x-request-id:

x-request-id
------------

The ``x-request-id`` header is used by Envoy to uniquely identify a request as well as perform stable
access logging and tracing. Envoy will generate an ``x-request-id`` header for all external origin
requests (the header is sanitized). It will also generate an ``x-request-id`` header for internal
requests that do not already have one. This means that ``x-request-id`` can and should be propagated
between client applications in order to have stable IDs across the entire mesh. Due to the out of
process architecture of Envoy, the header can not be automatically forwarded by Envoy itself. This
is one of the few areas where a thin client library is needed to perform this duty. How that is done
is out of scope for this documentation. If ``x-request-id`` is propagated across all hosts, the
following features are available:

* Stable :ref:`access logging <config_access_log>` via the
  :ref:`v3 API runtime filter<envoy_v3_api_field_config.accesslog.v3.AccessLogFilter.runtime_filter>`.
* Stable tracing when performing random sampling via the :ref:`tracing.random_sampling
  <config_http_conn_man_runtime_random_sampling>` runtime setting or via forced tracing using the
  :ref:`config_http_conn_man_headers_x-envoy-force-trace` and
  :ref:`config_http_conn_man_headers_x-client-trace-id` headers.

See the architecture overview on
:ref:`context propagation <arch_overview_tracing_context_propagation>` for more information.

.. _config_http_conn_man_headers_x-ot-span-context:

x-ot-span-context
-----------------

The ``x-ot-span-context`` HTTP header is used by Envoy to establish proper parent-child relationships
between tracing spans when used with the LightStep tracer.
For example, an egress span is a child of an ingress
span (if the ingress span was present). Envoy injects the ``x-ot-span-context`` header on ingress requests and
forwards it to the local service. Envoy relies on the application to propagate ``x-ot-span-context`` on
the egress call to an upstream. See more on tracing :ref:`here <arch_overview_tracing>`.

.. _config_http_conn_man_headers_x-b3-traceid:

x-b3-traceid
------------

The ``x-b3-traceid`` HTTP header is used by the Zipkin tracer in Envoy.
The TraceId is 64-bit in length and indicates the overall ID of the
trace. Every span in a trace shares this ID. See more on zipkin tracing
`here <https://github.com/openzipkin/b3-propagation>`__.

.. _config_http_conn_man_headers_x-b3-spanid:

x-b3-spanid
-----------

The ``x-b3-spanid`` HTTP header is used by the Zipkin tracer in Envoy.
The SpanId is 64-bit in length and indicates the position of the current
operation in the trace tree. The value should not be interpreted: it may or
may not be derived from the value of the TraceId. See more on zipkin tracing
`here <https://github.com/openzipkin/b3-propagation>`__.

.. _config_http_conn_man_headers_x-b3-parentspanid:

x-b3-parentspanid
-----------------

The ``x-b3-parentspanid`` HTTP header is used by the Zipkin tracer in Envoy.
The ParentSpanId is 64-bit in length and indicates the position of the
parent operation in the trace tree. When the span is the root of the trace
tree, the ParentSpanId is absent. See more on zipkin tracing
`here <https://github.com/openzipkin/b3-propagation>`__.

.. _config_http_conn_man_headers_x-b3-sampled:

x-b3-sampled
------------

The ``x-b3-sampled`` HTTP header is used by the Zipkin tracer in Envoy.
When the Sampled flag is either not specified or set to 1, the span will be reported to the tracing
system. Once Sampled is set to 0 or 1, the same
value should be consistently sent downstream. See more on zipkin tracing
`here <https://github.com/openzipkin/b3-propagation>`__.

.. _config_http_conn_man_headers_x-b3-flags:

x-b3-flags
----------

The ``x-b3-flags`` HTTP header is used by the Zipkin tracer in Envoy.
The encode one or more options. For example, Debug is encoded as
``X-B3-Flags: 1``. See more on zipkin tracing
`here <https://github.com/openzipkin/b3-propagation>`__.

.. _config_http_conn_man_headers_b3:

b3
----------

The ``b3`` HTTP header is used by the Zipkin tracer in Envoy.
Is a more compressed header format. See more on zipkin tracing
`here <https://github.com/openzipkin/b3-propagation#single-header>`__.

.. _config_http_conn_man_headers_x-datadog-trace-id:

x-datadog-trace-id
------------------

The ``x-datadog-trace-id`` HTTP header is used by the Datadog tracer in Envoy.
The 64-bit value represents the ID of the overall trace, and is used to correlate
the spans.

.. _config_http_conn_man_headers_x-datadog-parent-id:

x-datadog-parent-id
-------------------

The ``x-datadog-parent-id`` HTTP header is used by the Datadog tracer in Envoy.
The 64-bit value uniquely identifies the span within the trace, and is used to
create parent-child relationships between spans.

.. _config_http_conn_man_headers_x-datadog-sampling-priority:

x-datadog-sampling-priority
---------------------------

The ``x-datadog-sampling-priority`` HTTP header is used by the Datadog tracer in Envoy.
The integer value indicates the sampling decision that has been made for this trace.
A value of 0 indicates that the trace should not be collected, and a value of 1
requests that spans are sampled and reported.

.. _config_http_conn_man_headers_sw8:

sw8
----------

The ``sw8`` HTTP header is used by the SkyWalking tracer in Envoy. It contains the key
tracing context for the SkyWalking tracer and is used to establish the relationship between
the tracing spans of downstream and Envoy. See more on SkyWalking tracing
`here <https://github.com/apache/skywalking/blob/v8.1.0/docs/en/protocols/Skywalking-Cross-Process-Propagation-Headers-Protocol-v3.md>`__.

.. _config_http_conn_man_headers_x-amzn-trace-id:

x-amzn-trace-id
---------------

The ``x-amzn-trace-id`` HTTP header is used by the AWS X-Ray tracer in Envoy. The trace ID,
parent ID and sampling decision are added to HTTP requests in the tracing header. See more on AWS X-Ray tracing
`here <https://docs.aws.amazon.com/xray/latest/devguide/xray-concepts.html#xray-concepts-tracingheader>`__.

.. _config_http_conn_man_headers_custom_request_headers:

Custom request/response headers
-------------------------------

Custom request/response headers can be added to a request/response at the weighted cluster,
route, virtual host, and/or global route configuration level. See the
:ref:`v3 <envoy_v3_api_msg_config.route.v3.RouteConfiguration>` API documentation.

Neither ``:``-prefixed pseudo-headers nor the Host: header may be modified via this mechanism. The ``:path``
and ``:authority`` headers may instead be modified via mechanisms such as
:ref:`prefix_rewrite <envoy_v3_api_field_config.route.v3.RouteAction.prefix_rewrite>`,
:ref:`regex_rewrite <envoy_v3_api_field_config.route.v3.RouteAction.regex_rewrite>`, and
:ref:`host_rewrite <envoy_v3_api_field_config.route.v3.RouteAction.host_rewrite_literal>`.

Headers are appended to requests/responses in the following order: weighted cluster level headers,
route level headers, virtual host level headers and finally global level headers.

Envoy supports adding dynamic values to request and response headers. The percent symbol (%) is
used to delimit variable names.

.. attention::

  If a literal percent symbol (%) is desired in a request/response header, it must be escaped by
  doubling it. For example, to emit a header with the value ``100%``, the custom header value in
  the Envoy configuration must be ``100%%``.

All HTTP :ref:`Command Operators <config_access_log_command_operators>` used for access logging may
be specified in custom request/response headers. However, depending where a particular command
operator is used, the context needed for the operator may not be available and the produced output
is empty string. For example, the following configuration uses ``%RESPONSE_CODE%`` operator to
modify request headers using code from the response.  The output is an empty string, because request
headers are modified before the request is sent upstream and the response is not received yet.

.. literalinclude:: _include/header_formatters.yaml
    :language: yaml
    :linenos:
    :lines: 15-20
    :emphasize-lines: 3-6
    :caption: :download:`header_formatters.yaml <_include/header_formatters.yaml>`

.. attention::

  The following legacy header formatters are still supported, but will be deprecated in the future.
  The equivalent information can be accessed using indicated substitutes.

  ``%DYNAMIC_METADATA(["namespace", "key", ...])%``
    Populates the header with dynamic metadata available in a request
    (e.g.: added by filters like the header-to-metadata filter).

    This works both on request and response headers.

    Use :ref:`%DYNAMIC_METADATA(namespace:key:…):Z%<config_access_log_format_dynamic_metadata>` instead.

  ``%UPSTREAM_METADATA(["namespace", "key", ...])%``
    Populates the header with :ref:`EDS endpoint metadata <envoy_v3_api_field_config.endpoint.v3.LbEndpoint.metadata>` from the
    upstream host selected by the router. Metadata may be selected from any namespace. In general,
    metadata values may be strings, numbers, booleans, lists, nested structures, or null. Upstream
    metadata values may be selected from nested structs by specifying multiple keys. Otherwise,
    only string, boolean, and numeric values are supported. If the namespace or key(s) are not
    found, or if the selected value is not a supported type, then no header is emitted. The
    namespace and key(s) are specified as a JSON array of strings. Finally, percent symbols in the
    parameters **do not** need to be escaped by doubling them.

    Upstream metadata cannot be added to request headers as the upstream host has not been selected
    when custom request headers are generated.

    Use :ref:`%UPSTREAM_METADATA(namespace:key:…):Z%<config_access_log_format_upstream_host_metadata>` instead.

  ``%PER_REQUEST_STATE(reverse.dns.data.name)%``
    Populates the header with values set on the stream info ``filterState()`` object. To be
    usable in custom request/response headers, these values must be of type
    ``Envoy::Router::StringAccessor``. These values should be named in standard reverse DNS style,
    identifying the organization that created the value and ending in a unique name for the data.

    Use :ref:`%FILTER_STATE(reverse.dns.data.name:PLAIN):Z%<config_access_log_format_filter_state>` instead.
