.. _faq_api_v3_config:

How do I configure Envoy to use the v3 API?
===========================================

All bootstrap files are expected to be v3.

For dynamic configuration, we have introduced two new fields to :ref:`config sources
<envoy_v3_api_msg_config.core.v3.ConfigSource>`, transport API version and resource API version. The
distinction is as follows:

* The :ref:`transport API version
  <envoy_v3_api_field_config.core.v3.ApiConfigSource.transport_api_version>` indicates the API
  endpoint and version of *DiscoveryRequest*/*DiscoveryResponse* messages used.

* The :ref:`resource API version
  <envoy_v3_api_field_config.core.v3.ConfigSource.resource_api_version>` indicates whether a v2 or
  v3 resource, e.g. v2 *RouteConfiguration* or v3 *RouteConfiguration*, is delivered.

The API version must be set for both transport and resource API versions.

If you see a warning or error with ``V2 (and AUTO) xDS transport protocol versions are deprecated``,
it is likely that you are missing explicit V3 configuration of the transport API version.
