.. _client_features:

Well Known Client Features
==========================

Authoritative list of features that an xDS client may support. An xDS client supplies the list of
features it supports in the :ref:`client_features <envoy_api_field_core.Node.client_features>` field.
Client features use reverse DNS naming scheme, for example `com.acme.feature`.

Currently Defined Client Features
---------------------------------

+------------------------------------------------+-------------------------------------------------+
| Client Feature Name                            | Description                                     |
+================================================+=================================================+
| envoy.config.require-any-fields-contain-struct | This feature indicates that xDS client requires |
|                                                | that the configuration entries of type          |
|                                                | *google.protobuf.Any* contain messages of type  |
|                                                | *udpa.type.v1.TypedStruct* only                 |
+------------------------------------------------+-------------------------------------------------+
