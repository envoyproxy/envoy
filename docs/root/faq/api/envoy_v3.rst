How do I configure Envoy to use the v3 API?
===========================================

By default, Envoy will attempt to parse any YAML, JSON or text proto as v2, and if it fails to do
so, will consider it as v3. So, if you have a simple static Envoy consuming a text-based bootstrap,
you just need to start using the new configuration. For binary proto bootstrap configuration, please
use a :ref:`v3 Bootstrap <envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` proto.

For dynamic configuration, we have introduced two new fields to :ref:`config sources
<envoy_v3_api_msg_config.core.v3.ConfigSource>`, transport API version and resource API version. The
distinction is as follows:

* The :ref:`transport API version
  <envoy_v3_api_field_config.core.v3.ApiConfigSource.transport_api_version>` indicates the API
  endpoint and version of *DiscoveryRequest*/*DiscoveryResponse* messages used.

* The :ref:`resource API version
  <envoy_v3_api_field_config.core.v3.ConfigSource.resource_api_version>` indicates whether a v2 or
  v3 resource, e.g. v2 *RouteConfiguration* or v3 *RouteConfiguration*, is delivered.

It is possible to use a mixture of transport API and resource API versions, e.g. to deliver v2
*Listener* resources and v3 *RouteConfiguration* resources over a v2 ADS transport. This is an
intentional feature designed to provide for gradual migration of Envoy deployments from v2 to v3.

There may be some operational advantage in having vM resources delivered over vN endpoints, so we
provide the flexibility to make this call by appropriate configuration of :ref:`config sources
<envoy_v3_api_msg_config.core.v3.ConfigSource>`.
