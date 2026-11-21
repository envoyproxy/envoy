Added :ref:`proxy_transport_socket
<envoy_v3_api_field_extensions.transport_sockets.http_11_proxy.v3.Http11ProxyUpstreamTransport.proxy_transport_socket>`,
:ref:`proxy_authorization
<envoy_v3_api_field_extensions.transport_sockets.http_11_proxy.v3.Http11ProxyUpstreamTransport.proxy_authorization>`
and :ref:`proxy_basic_auth
<envoy_v3_api_field_extensions.transport_sockets.http_11_proxy.v3.Http11ProxyUpstreamTransport.proxy_basic_auth>`
to the HTTP/1.1 proxy transport socket. They allow connecting to the forward proxy over TLS (or any
other transport socket) and sending a ``Proxy-Authorization`` header in the ``CONNECT`` request,
either sourced from a generic secret or built from a username and password. When any of them is set,
all proxied connections, including plaintext ones, are tunneled with ``CONNECT``.
