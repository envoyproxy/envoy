Added a new dynamic modules transport socket extension (``envoy.transport_sockets.dynamic_modules``)
that delegates connection I/O to a transport socket implemented in a dynamic module loaded via
``dlopen``. The module performs raw socket reads and writes through ``io_read`` and ``io_write``
callbacks and transforms the bytes that flow over the connection, for example to implement a custom
encryption scheme. The module can also negotiate secure transport for the STARTTLS pattern and
inspect the connection's local and remote addresses. The extension can be configured on both
downstream listeners and upstream clusters. The Rust SDK exposes this through a ``TransportSocket``
interface and a matching transport socket init function. See
:ref:`DynamicModuleTransportSocket <envoy_v3_api_msg_extensions.transport_sockets.dynamic_modules.v3.DynamicModuleTransportSocket>`
for configuration details.
