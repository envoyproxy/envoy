.. _arch_overview_attributes:

Attributes
==========

Attributes refer to contextual properties provided by Envoy during request and
connection processing. They are named by a dot-separated path (e.g.
``request.path``), have a fixed type (e.g. ``string`` or ``int``), and may be
absent or present depending on the context. Attributes are exposed to CEL
runtime in :ref:`RBAC filter <arch_overview_rbac>`, as well as Wasm extensions
via ``get_property`` ABI method.

Attribute value types are limited to:

* ``string`` for UTF-8 strings
* ``bytes`` for byte buffers
* ``int`` for 64-bit signed integers
* ``uint`` for 64-bit unsigned integers
* ``bool`` for booleans
* ``list`` for lists of values
* ``map`` for associative arrays with string keys
* ``timestamp`` for timestamps as specified by `Timestamp <https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#timestamp>`_
* ``duration`` for durations as specified by `Duration <https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#duration>`_
* Protocol buffer message types

CEL provides standard helper functions for operating on abstract types such as
``getMonth`` for ``timestamp`` values. Note that integer literals (e.g. ``7``) are of
type ``int``, which is distinct from ``uint`` (e.g. ``7u``), and the arithmetic
conversion is not automatic (use ``uint(7)`` for explicit conversion).

Wasm extensions receive the attribute values as a serialized buffer according
to the type of the attribute. Strings and bytes are passed as-is, integers are
passed as 64 bits directly, timestamps and durations are approximated to
nano-seconds, and structured values are converted to a sequence of pairs
recursively.

.. _arch_overview_request_attributes:

Request attributes
------------------

The following request attributes are generally available upon initial request
processing, which makes them suitable for RBAC policies.

``request.*`` attributes are only available in http filters.

.. csv-table::
   :header: Attribute, Type, Description
   :escape: '
   :widths: 1, 1, 4

   request.path, string, The path portion of the URL
   request.url_path, string, The path portion of the URL without the query string
   request.host, string, The host portion of the URL
   request.scheme, string, The scheme portion of the URL e.g. "http"
   request.method, string, Request method e.g. "GET"
   request.headers, "map<string, string>", All request headers indexed by the lower-cased header name
   request.referer, string, Referer request header
   request.useragent, string, User agent request header
   request.time, timestamp, Time of the first byte received
   request.id, string, Request ID corresponding to ``x-request-id`` header value
   request.protocol, string, "Request protocol ('"HTTP/1.0'", '"HTTP/1.1'", '"HTTP/2'", or '"HTTP/3'")"
   request.query, string, The query portion of the URL in the format of "name1=value1&name2=value2".

Header values in ``request.headers`` associative array are comma-concatenated in case of multiple values.

Additional attributes are available once the request completes:

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   request.duration, duration, Total duration of the request
   request.size, int, Size of the request body. Content length header is used if available.
   request.total_size, int, Total size of the request including the approximate uncompressed size of the headers

Response attributes
-------------------

Response attributes are only available after the request completes.

``response.*`` attributes are only available in http filters.

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   response.code, int, Response HTTP status code
   response.code_details, string, Internal response code details (subject to change)
   response.flags, int, Additional details about the response beyond the standard response code encoded as a bit-vector
   response.grpc_status, int, Response gRPC status code
   response.headers, "map<string, string>", All response headers indexed by the lower-cased header name
   response.trailers, "map<string, string>", All response trailers indexed by the lower-cased trailer name
   response.size, int, Size of the response body
   response.total_size, int, Total size of the response including the approximate uncompressed size of the headers and the trailers
   response.backend_latency, duration, Duration between the first byte sent to and the last byte received from the upstream backend

Connection attributes
---------------------

The following attributes are available once the downstream connection is
established (which also applies to HTTP requests making them suitable for
RBAC):

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   source.address, string, Downstream connection remote address
   source.port, int, Downstream connection remote port
   destination.address, string, Downstream connection local address
   destination.port, int, Downstream connection local port
   connection.id, uint, Downstream connection ID
   connection.mtls, bool, Indicates whether TLS is applied to the downstream connection and the peer ceritificate is presented
   connection.requested_server_name, string, Requested server name in the downstream TLS connection
   connection.tls_version, string, TLS version of the downstream TLS connection
   connection.subject_local_certificate, string, The subject field of the local certificate in the downstream TLS connection
   connection.subject_peer_certificate, string, The subject field of the peer certificate in the downstream TLS connection
   connection.dns_san_local_certificate, string, The first DNS entry in the SAN field of the local certificate in the downstream TLS connection
   connection.dns_san_peer_certificate, string, The first DNS entry in the SAN field of the peer certificate in the downstream TLS connection
   connection.uri_san_local_certificate, string, The first URI entry in the SAN field of the local certificate in the downstream TLS connection
   connection.uri_san_peer_certificate, string, The first URI entry in the SAN field of the peer certificate in the downstream TLS connection
   connection.sha256_peer_certificate_digest, string, SHA256 digest of the peer certificate in the downstream TLS connection if present
   connection.transport_failure_reason, string, The transport failure reason e.g. certificate validation failed

The following additional attributes are available upon the downstream connection termination:

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   connection.termination_details, string, Internal termination details of the connection (subject to change)

Upstream attributes
-------------------

The following attributes are available once the upstream connection is established:

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   upstream.address, string, Upstream connection remote address
   upstream.port, int, Upstream connection remote port
   upstream.tls_version, string, TLS version of the upstream TLS connection
   upstream.subject_local_certificate, string, The subject field of the local certificate in the upstream TLS connection
   upstream.subject_peer_certificate, string, The subject field of the peer certificate in the upstream TLS connection
   upstream.dns_san_local_certificate, string, The first DNS entry in the SAN field of the local certificate in the upstream TLS connection
   upstream.dns_san_peer_certificate, string, The first DNS entry in the SAN field of the peer certificate in the upstream TLS connection
   upstream.uri_san_local_certificate, string, The first URI entry in the SAN field of the local certificate in the upstream TLS connection
   upstream.uri_san_peer_certificate, string, The first URI entry in the SAN field of the peer certificate in the upstream TLS connection
   upstream.sha256_peer_certificate_digest, string, SHA256 digest of the peer certificate in the upstream TLS connection if present
   upstream.local_address, string, The local address of the upstream connection
   upstream.transport_failure_reason, string, The upstream transport failure reason e.g. certificate validation failed

Metadata and filter state
-------------------------

Data exchanged between filters is available as the following attributes:

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   metadata, :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`, Dynamic request metadata
   filter_state, "map<string, Value>", Mapping from the filter state name to the object value
   upstream_filter_state, "map<string, Value>", Mapping from the upstream filter state name to the object value

Filter state value representation is determined based on the filter state
declaration in the following order:

* If the value is represented as a dynamic ``CelValue`` wrapper, ``CelValue``
  is returned verbatim.
* If the key is well-known and has a field reflection enabled, then it is
  returned as a map from the field names to the field values.
* Otherwise, the value is returned as the serialized bytes.

Note that these attributes may change during the life of a request as the data can be
updated by filters at any point.

Configuration attributes
----------------------------

Configuration identifiers and metadata related to the handling of the request or the connection is available as the
following attributes:

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   xds.node, :ref:`Node<envoy_v3_api_msg_config.core.v3.node>`, Local node description
   xds.cluster_name, string, Upstream cluster name
   xds.cluster_metadata, :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`, Upstream cluster metadata
   xds.listener_direction, int, Enumeration value of the :ref:`listener traffic direction<envoy_v3_api_field_config.listener.v3.Listener.traffic_direction>`
   xds.listener_metadata, :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`, Listener metadata
   xds.route_name, string, Route name
   xds.route_metadata, :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`, Route metadata
   xds.upstream_host_metadata, :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`, Upstream host metadata
   xds.filter_chain_name, string, Listener filter chain name


Wasm attributes
---------------

In addition to all above, the following extra attributes are available to Wasm extensions:

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   plugin_name, string, Plugin name
   plugin_root_id, string, Plugin root ID
   plugin_vm_id, string, Plugin VM ID
   node, :ref:`Node<envoy_v3_api_msg_config.core.v3.node>`, Local node description. DEPRECATED: please use `xds` attributes.
   cluster_name, string, Upstream cluster name. DEPRECATED: please use `xds` attributes.
   cluster_metadata, :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`, Upstream cluster metadata. DEPRECATED: please use `xds` attributes.
   listener_direction, int, Enumeration value of the :ref:`listener traffic direction<envoy_v3_api_field_config.listener.v3.Listener.traffic_direction>`. DEPRECATED: please use `xds` attributes.
   listener_metadata, :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`, Listener metadata. DEPRECATED: please use `xds` attributes.
   route_name, string, Route name. DEPRECATED: please use `xds` attributes.
   route_metadata, :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`, Route metadata. DEPRECATED: please use `xds` attributes.
   upstream_host_metadata, :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`, Upstream host metadata. DEPRECATED: please use `xds` attributes.

Path expressions
----------------

Path expressions allow access to inner fields in structured attributes via a
sequence of field names, map, and list indexes following an attribute name. For
example, ``get_property({"node", "id"})`` in Wasm ABI extracts the value of ``id``
field in ``node`` message attribute, while ``get_property({"request", "headers",
"my-header"})`` refers to the comma-concatenated value of a particular request
header.
