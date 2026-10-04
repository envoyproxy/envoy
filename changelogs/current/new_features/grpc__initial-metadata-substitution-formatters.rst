Added the ability for :ref:`initial_metadata <envoy_v3_api_field_config.core.v3.GrpcService.initial_metadata>`
header values to use substitution formatters, including formatter extensions declared in the new
:ref:`formatters <envoy_v3_api_field_config.core.v3.GrpcService.formatters>` field (for example
``%SECRET(name)%`` to retrieve an authentication token from an SDS secret without embedding it in
the Envoy configuration).
