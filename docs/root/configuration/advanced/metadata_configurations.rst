.. _metadata_configurations:

Metadata configurations
=======================

Envoy utilizes :ref:`metadata <envoy_v3_api_msg_config.core.v3.Metadata>` to transport arbitrary untyped or typed
data from the control plane to Envoy. Metadata configurations can be applied to Listeners, clusters, routes, virtual hosts,
endpoints, and other elements.


Unlike other configurations, Envoy does not explicitly define the purpose of metadata configurations, which can be used for
stats, logging, or filter/extension behavior. Users can define the purpose of metadata configurations for their specific
use cases. Metadata configurations offer a flexible way to transport user-defined data from the control plane to Envoy without
modifying Envoy's core API or implementation.


For instance, users can add extra attributes to routes, such as the route owner or upstream service maintainer, to metadata.
They can then enable Envoy to log these attributes to the access log or report them to StatsD, among other possibilities.
Moreover, users can write a filter/extension to read these attributes and execute any specific logic.
