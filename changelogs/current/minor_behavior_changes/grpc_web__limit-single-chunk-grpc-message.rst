The gRPC-Web filter now limits the ``grpc-message`` built from a non-gRPC response body to 16 KiB
also when the body arrives as a single chunk (for example a local reply), as it already did for
bodies that arrive in several chunks.
