Added default stats tag-extraction rules for the ``envoy.google_grpc_client_prefix`` tag,
extracting the gRPC client ``stat_prefix`` from the xDS subscription stats
``listener_manager.lds.grpc.<stat_prefix>.*``, ``sds.<resource_name>.grpc.<stat_prefix>.*`` and
``extension_config_discovery.<config_name>.grpc.<stat_prefix>.*``. Flat stat names are unchanged.
