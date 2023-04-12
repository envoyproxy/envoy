.. _config_http_filters_geoip:

IP Geolocation Filter
=========================
This filter decorates HTTP requests with the geolocation data.
Filter uses client address to lookup information (eg client's city, country) in the geolocation provider database.
Upon a successful lookup request will be enriched with the configured geolocation header and value from the database.
In case the configured geolocation headers are present in the incoming request, they will be overriden by the filter.
Geolocation filter emits stats for the number of successful lookups and the number of total lookups.
As for now, only `Maxmind <https://www.maxmind.com/en/geoip2-services-and-databases>` geolocation provider is supported.

Configuration
-------------
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.geoip.v3.Geoip``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.geoip.v3.Geoip>`

Statistics
----------
Geolocation filter outputs statistics in the
*http.<stat_prefix>.geoip.<geo-header-name>.* namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: auto

  .hit, Counter, Number of successful lookups within geolocation database for a configured geolocation header.
  .total, Counter, Number of total lookups within geolocation database for a configured geolocation header.


Configuration example
---------------------

.. code-block:: yaml

  name: envoy.filters.http.geoip
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.geoip.v3.Geoip
    use_xff: true
    xff_num_trusted_hops: 1
    geo_headers_to_add:
      country: "x-geo-country"
      region: "x-geo-region"
    provider:
        name: "envoy.geoip_providers.maxmind"

