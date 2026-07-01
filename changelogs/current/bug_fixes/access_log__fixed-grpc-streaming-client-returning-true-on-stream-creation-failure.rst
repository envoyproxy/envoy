Fixed a bug in the gRPC streaming access log client where a stream creation failure incorrectly
returned ``true`` from ``log()``, preventing ``grpc_entries_flush_failed`` from being incremented
in that path.
