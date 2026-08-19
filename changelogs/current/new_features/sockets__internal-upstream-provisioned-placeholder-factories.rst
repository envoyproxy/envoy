Added
:ref:`provisioned_placeholder_factories <envoy_v3_api_field_extensions.transport_sockets.internal_upstream.v3.InternalUpstreamTransport.provisioned_placeholder_factories>`
to the ``internal_upstream`` transport socket, naming registered filter state object factories to
provision as empty objects on the upstream connection and share by reference with the downstream
internal connection. A filter on the internal connection can populate such an object in place, and
the value is then readable on the upstream connection.
