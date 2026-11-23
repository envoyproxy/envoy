EDS and static cluster hosts now share endpoint and locality metadata only when it serializes to the
same bytes. Endpoints and localities whose
:ref:`typed_filter_metadata <envoy_v3_api_field_config.core.v3.Metadata.typed_filter_metadata>`
holds equal messages encoded differently, for example by a control plane that does not serialize
map fields deterministically, no longer share one in-memory copy of that metadata, while an update
still treats endpoint metadata as unchanged.
