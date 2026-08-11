Fixed a crash in the TCP, HTTP, gRPC, and Thrift active health checkers that could occur when the
upstream health check connection could not be created, for example when the configured Linux
network namespace (:ref:`network_namespace_filepath
<envoy_v3_api_field_config.core.v3.SocketAddress.network_namespace_filepath>`) became unavailable at
runtime. The health check now reports a network failure instead of dereferencing a null connection.
