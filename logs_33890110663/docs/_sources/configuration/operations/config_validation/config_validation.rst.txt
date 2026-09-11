.. _config_config_validation:

Config Validations
==================

Envoy supports dynamic configuration using the :ref:`xDS protocol <xds_protocol>`.
When receiving a new configuration, Envoy first verifies that all fields are valid,
according to their protoc-gen-validate (PGV) constraints, and that the new config
keeps Envoy's internal state correct. If a given config violates a constraint, that
config is rejected (see :ref:`ACK/NACK and resource type instance version <xds_ack_nack>`).

In addition to the above, Envoy also supports custom config validations where
verifications can be made, when using :ref:`gRPC-based subscriptions <xds_protocol_streaming_grpc_subscriptions>` .
An example of such a validator is where an Envoy instance is expected to always have a
minimal number of clusters, then any config update that results with a number of
clusters which is less than the threshold should be rejected.

Custom config validators are defined using Envoy's :ref:`extension <extending>` framework.
Envoy's builtin config validators are listed :ref:`here <v3_config_config_validators>`.

To use a config validation extension, it needs to be added to the
:ref:`config_validators list <envoy_v3_api_field_config.core.v3.ApiConfigSource.config_validators>`
field of the API configuration source that will be validated.
