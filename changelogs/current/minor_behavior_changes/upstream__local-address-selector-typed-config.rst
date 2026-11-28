Envoy now unpacks and validates the ``typed_config`` of a cluster's or the bootstrap's
:ref:`local_address_selector <envoy_v3_api_field_config.core.v3.BindConfig.local_address_selector>`
for every selector whose config type is not ``google.protobuf.Empty``. Previously the config was
only used to look up the selector. Fields that were ignored before can now cause a warning, or a
config rejection under strict validation.
