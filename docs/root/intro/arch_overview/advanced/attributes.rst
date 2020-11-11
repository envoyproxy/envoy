.. _arch_overview_attributes:

Attributes
==========

Attributes refer to contextual properties provided by Envoy during request and
connection processing. They are named by a dot-separated path (e.g.
`request.path`), have a fixed type (e.g. `string` or `int64`), and may be
absent or present depending on the context. Attributes are exposed to CEL
runtime in :ref:`RBAC filter <arch_overview_rbac>`, as well as Wasm extensions
via `get_property` ABI method.

Request attributes
------------------

The following request attributes are generally available upon initial request
processing, which makes them suitable for RBAC policies:

.. csv-table::
   :header: Attribute, Type, Description
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
   request.id, string, Request ID
   request.protocol, string, Request protocol e.g. "HTTP/2"

Additional attributes are available once the request is finished:

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   request.duration, duration, Total duration of the request
   request.size, int64, Size of the request body. Content length header is used if available.
   request.total_size, int64, Total size of the request including the approximate uncompressed size of the headers

Response attributes
-------------------

Response attributes are only available after the request is finished.

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   response.code, int64, Response HTTP status code
   response.code_details, string, Internal response code details (subject to change)
   response.flags, int64, Additional details about the response beyond the standard response code encoded as a bit-vector
   response.grpc_status, int64, Response gRPC status code
   response.headers, "map<string, string>", All response headers indexed by the lower-cased header name
   response.trailers, "map<string, string>", All response trailers indexed by the lower-cased trailer name
   response.size, int64, Size of the response body
   response.total_size, int64, Total size of the response including the approximate uncompressed size of the headers and the trailers

Connection attributes
---------------------

The following attributes are available once the downstream connection is
established (which also applies to HTTP requests, so they can be used for
RBAC):

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   source.address, string, Downstream connection remote address
   source.port, int64, Downstream connection remote port
   destination.address, string, Downstream connection local address
   destination.port, int64, Downstream connection local port
   connection.id, uint64, Downstream connection ID
   connection.mtls, bool, Indicates whether TLS is applied to the downstream connection and the peer ceritificate is presented
   connection.requested_server_name, string, Requested server name in the downstream TLS connection
   connection.tls_version, string, TLS version of the downstream TLS connection
   connection.subject_local_certificate, string, The subject field of the local certificate in the downstream TLS connection
   connection.subject_peer_certificate, string, The subject field of the peer certificate in the downstream TLS connection
   connection.dns_san_local_certificate, string, The first DNS entry in the SAN field of the local certificate in the downstream TLS connection
   connection.dns_san_peer_certificate, string, The first DNS entry in the SAN field of the peer certificate in the downstream TLS connection
   connection.uri_san_local_certificate, string, The first URI entry in the SAN field of the local certificate in the downstream TLS connection
   connection.uri_san_peer_certificate, string, The first URI entry in the SAN field of the peer certificate in the downstream TLS connection

The following additional attributes are available upon the downstream connection termination:

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   connection.termination_details, string, Internal termination details of the connection (subject to change)

Upstream attributes
-------------------

The following attribues are available once the upstream connection is established:

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   upstream.address, string, Upstream connection remote address
   upstream.port, int64, Upstream connection remote port
   upstream.tls_version, string, TLS version of the upstream TLS connection
   upstream.subject_local_certificate, string, The subject field of the local certificate in the upstream TLS connection
   upstream.subject_peer_certificate, string, The subject field of the peer certificate in the upstream TLS connection
   upstream.dns_san_local_certificate, string, The first DNS entry in the SAN field of the local certificate in the upstream TLS connection
   upstream.dns_san_peer_certificate, string, The first DNS entry in the SAN field of the peer certificate in the upstream TLS connection
   upstream.uri_san_local_certificate, string, The first URI entry in the SAN field of the local certificate in the upstream TLS connection
   upstream.uri_san_peer_certificate, string, The first URI entry in the SAN field of the peer certificate in the upstream TLS connection
   upstream.local_address, string, The local address of the upstream connection
   upstream.transport_failure_reason, string, The upstream transport failure reason e.g. certificate validation failed

Metadata and filter state
-------------------------

Data exchanged between filters is available as the following attributes:

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   metadata, :ref:`Metadata<envoy_api_msg_core.Metadata>`, Dynamic request metadata
   filter_state, "map<string, bytes>", Mapping from a filter state name to its serialized string value

Note that these attributes may change during the life of a request as the data can be
updated by filters at any point.

Wasm attributes
---------------

In addition to all above, the following extra attributes are available to Wasm extensions:

.. csv-table::
   :header: Attribute, Type, Description
   :widths: 1, 1, 4

   plugin_name, string, Plugin name
   plugin_root_id, string, Plugin root ID
   plugin_vm_id, string, Plugin VM ID
   node, :ref:`Node<envoy_api_msg_core.Node>`, Local node description
   cluster_name, string, Upstream cluster name
   cluster_metadata, :ref:`Metadata<envoy_api_msg_core.Metadata>`, Upstream cluster metadata
   listener_direction, int64, Enumeration value of the :ref:`listener traffic direction<envoy_v3_api_field_config.listener.v3.Listener.traffic_direction>`
   listener_metadata, :ref:`Metadata<envoy_api_msg_core.Metadata>`, Listener metadata
   route_name, string, Route name
   route_metadata, :ref:`Metadata<envoy_api_msg_core.Metadata>`, Route metadata
   upstream_host_metadata, :ref:`Metadata<envoy_api_msg_core.Metadata>`, Upstream host metadata

