.. _arch_overview_rbac:

Role Based Access Control
=========================

* :ref:`Network filter configuration <config_network_filters_rbac>`.
* :ref:`HTTP filter configuration <config_http_filters_rbac>`.

The Role Based Access Control (RBAC) filter checks if the incoming request is authorized or not.
Unlike external authorization, the check of RBAC filter happens in the Envoy process and is
based on a list of policies from the filter config.

The RBAC filter can be either configured as a :ref:`network filter <config_network_filters_rbac>`,
or as a :ref:`HTTP filter <config_http_filters_rbac>` or both. If the request is deemed unauthorized
by the network filter then the connection will be closed. If the request is deemed unauthorized by
the HTTP filter the request will be denied with 403 (Forbidden) response.

Policy
------

The RBAC filter checks the request based on a list of
:ref:`policies <envoy_api_field_config.rbac.v2.RBAC.policies>`. A policy consists of a list of
:ref:`permissions <envoy_api_msg_config.rbac.v2.Permission>` and
:ref:`principals <envoy_api_msg_config.rbac.v2.Principal>`. The permission specifies the actions of
the request, for example, the method and path of a HTTP request. The principal specifies the
downstream client identities of the request, for example, the URI SAN of the downstream client
certificate. A policy is matched if its permissions and principals are matched at the same time.

Shadow Policy
-------------

The filter can be configured with a
:ref:`shadow policy <envoy_api_field_config.filter.http.rbac.v2.RBAC.shadow_rules>` that doesn't
have any effect (i.e. not deny the request) but only emit stats and log the result. This is useful
for testing a rule before applying in production.

.. _arch_overview_condition:

Condition
---------

In addition to the pre-defined permissions and principals, a policy may optionally provide an
authorization condition written in the `Common Expression Language
<https://github.com/google/cel-spec/blob/master/doc/intro.md>`_. The condition specifies an extra
clause that must be satisfied for the policy to match. For example, the following condition checks
whether the request path starts with `/v1/`:

.. code-block:: yaml

  call_expr:
    function: startsWith
    args:
    - select_expr:
       operand:
         ident_expr:
           name: request
       field: path
    - const_expr:
       string_value: /v1/

The following attributes are exposed to the language runtime:

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 2

   request.path, string, The path portion of the URL
   request.url_path, string, The path portion of the URL without the query string
   request.host, string, The host portion of the URL
   request.scheme, string, The scheme portion of the URL
   request.method, string, Request method
   request.headers, string map, All request headers
   request.referer, string, Referer request header
   request.useragent, string, User agent request header
   request.time, timestamp, Time of the first byte received
   request.duration, duration, Total duration of the request
   request.id, string, Request ID
   request.size, int, Size of the request body
   request.total_size, int, Total size of the request including the headers
   response.code, int, Response HTTP status code
   response.headers, string map, All response headers
   response.trailers, string map, All response trailers
   response.size, int, Size of the response body
   source.address, string, Downstream connection remote address
   source.port, int, Downstream connection remote port
   destination.address, string, Downstream connection local address
   destination.port, int, Downstream connection local port
   metadata, :ref:`Metadata<envoy_api_msg_core.Metadata>`, Dynamic metadata
   connection.mtls, bool, Indicates whether TLS is applied to the downstream connection and the peer ceritificate is presented
   connection.requested_server_name, string, Requested server name in the downstream TLS connection
   connection.tls_version, string, TLS version of the downstream TLS connection
   connection.subject_local_certificate, string, The subject field of the local certificate in the downstream TLS connection
   connection.subject_peer_certificate, string, The subject field of the peer certificate in the downstream TLS connection
   connection.dns_san_local_certificate, string, The first DNS entry in the SAN field of the local certificate in the downstream TLS connection
   connection.dns_san_peer_certificate, string, The first DNS entry in the SAN field of the peer certificate in the downstream TLS connection
   connection.uri_san_local_certificate, string, The first URI entry in the SAN field of the local certificate in the downstream TLS connection
   connection.uri_san_peer_certificate, string, The first URI entry in the SAN field of the peer certificate in the downstream TLS connection
   upstream.address, string, Upstream connection remote address
   upstream.port, int, Upstream connection remote port
   upstream.tls_version, string, TLS version of the upstream TLS connection
   upstream.subject_local_certificate, string, The subject field of the local certificate in the upstream TLS connection
   upstream.subject_peer_certificate, string, The subject field of the peer certificate in the upstream TLS connection
   upstream.dns_san_local_certificate, string, The first DNS entry in the SAN field of the local certificate in the upstream TLS connection
   upstream.dns_san_peer_certificate, string, The first DNS entry in the SAN field of the peer certificate in the upstream TLS connection
   upstream.uri_san_local_certificate, string, The first URI entry in the SAN field of the local certificate in the upstream TLS connection
   upstream.uri_san_peer_certificate, string, The first URI entry in the SAN field of the peer certificate in the upstream TLS connection


Most attributes are optional and provide the default value based on the type of the attribute.
CEL supports presence checks for attributes and maps using `has()` syntax, e.g.
`has(request.referer)`.
