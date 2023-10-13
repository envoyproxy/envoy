.. _metadata_configurations:

Metadata configurations
=======================

:ref:`Metadata <envoy_v3_api_msg_config.core.v3.Metadata>` is widely used in Envoy to carry arbitrary untyped or
typed data from control plane to Envoy. Listeners, clusters, routes, virtual hosts, and endpoints, etc. can all carry
metadata configurations.


Unlike other configurations, Envoy does not explicitly define the purpose of metadata configurations. Basically,
metadata configurations could be used for stats, logging, and varying filter/extension behavior. The users could
define the purpose of metadata configurations based on their own use cases. Metadata configurations provde a flexible
way to carry user-defined data from control plane to Envoy and without change Envoy's core API and implementation.


For example, users could add additional attributes to routes, such as the owner of the route, the maintainer of the
upstream service, etc., to metadata. Then, they could enable Envoy to log these attributes to the access log or
report them to StatsD, among other possibilities. Additionally, users could write a filter/extension to read these
attributes and perform any specific logic.
