The default tag extractor now uses the full Google gRPC client stat prefix as
``envoy.google_grpc_client_prefix`` for LDS, SDS and ECDS ``streams_total`` and
``streams_closed_<code>`` counters. In legacy tag extraction, SDS and ECDS flat names with
two possible ``.grpc.`` separators are left without a client tag. Flat counter names are preserved.
