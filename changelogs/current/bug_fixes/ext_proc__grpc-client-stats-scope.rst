Fixed the stats scope that is used to create the gRPC client of the external processing
(``ext_proc``) filter. Previously the filter's own scope was used, so the gRPC client stats
gained an unexpected extra prefix,
``cluster.<cluster_name>.`` for an upstream filter. The server scope is now used, so the Google
gRPC client stats are emitted with the expected ``grpc.<google_grpc_stat_prefix>.`` prefix.
