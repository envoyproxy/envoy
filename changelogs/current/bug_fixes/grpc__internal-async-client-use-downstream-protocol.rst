Fixed an issue where internal gRPC async client (such as ``envoy_grpc``) requests sent HTTP/1.1
requests when the upstream cluster was configured with ``use_downstream_protocol_config`` by
setting HTTP/2 protocol on the stream.
