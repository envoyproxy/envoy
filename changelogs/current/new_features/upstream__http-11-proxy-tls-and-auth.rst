Added :ref:`proxy_transport_socket
<envoy_v3_api_field_extensions.transport_sockets.http_11_proxy.v3.Http11ProxyUpstreamTransport.proxy_transport_socket>`
and :ref:`proxy_authorization
<envoy_v3_api_field_extensions.transport_sockets.http_11_proxy.v3.Http11ProxyUpstreamTransport.proxy_authorization>`
to the HTTP/1.1 proxy transport socket. They allow connecting to the forward proxy over TLS (or any
other transport socket) and sending a ``Proxy-Authorization`` header, sourced from a generic secret,
in the ``CONNECT`` request. When either is set, all proxied connections, including plaintext ones,
are tunneled with ``CONNECT``.
