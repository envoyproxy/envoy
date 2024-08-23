.. _config_http_filters_geoip:

IP Geolocation Filter
=========================
This filter decorates HTTP requests with the geolocation data.
Filter uses client address to lookup information (e.g., client's city, country) in the geolocation provider database.
Upon a successful lookup request will be enriched with the configured geolocation header and the value from the database.
In case the configured geolocation headers are present in the incoming request, they will be overriden by the filter.
Geolocation filter emits stats for the number of the successful lookups and the number of total lookups.
English language is used for the geolocation lookups, the result of the lookup will be UTF-8 encoded.
Please note that Geolocation filter and providers are not yet supported on Windows.

Configuration
-------------
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.geoip.v3.Geoip``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.geoip.v3.Geoip>`

.. _config_geoip_providers_maxmind:

Geolocation Providers
---------------------
Currently only `Maxmind <https://www.maxmind.com/en/geoip2-services-and-databases>`_ geolocation provider is supported.
This provider should be configured with the type URL ``type.googleapis.com/envoy.extensions.geoip_providers.maxmind.v3.MaxMindConfig``.

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.geoip_providers.maxmind.v3.MaxMindConfig>`

.. _config_geoip_providers_common:

* :ref:`Common provider configuration <envoy_v3_api_msg_extensions.geoip_providers.common.v3.CommonGeoipProviderConfig>`

Configuration example
---------------------

.. code-block:: yaml

  name: envoy.filters.http.geoip
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.geoip.v3.Geoip
    xff_config:
      xff_num_trusted_hops: 1
    provider:
        name: "envoy.geoip_providers.maxmind"
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.geoip_providers.maxmind.v3.MaxMindConfig
          common_provider_config:
            geo_headers_to_add:
              country: "x-geo-country"
              region: "x-geo-region"
              city: "x-geo-city"
              asn: "x-geo-asn"
          city_db_path: "geoip/GeoLite2-City-Test.mmdb"
          isp_db_path: "geoip/GeoLite2-ASN-Test.mmdb"


Statistics
-------------

Geolocation HTTP filter has a statistics tree rooted at ``http.<stat_prefix>.``. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   ``rq_total``, Counter, Total number of requests for which geolocation filter was invoked.

Besides Geolocation filter level statisctics, there is statistics emitted by the :ref:`Maxmind geolocation provider <envoy_v3_api_msg_extensions.geoip_providers.maxmind.v3.MaxMindConfig>`
per geolocation database type (rooted at ``<stat_prefix>.maxmind.``). Database type can be one of `city_db <https://www.maxmind.com/en/geoip2-city>`_,
`isp_db <https://www.maxmind.com/en/geoip2-isp-database>`_, `anon_db <https://dev.maxmind.com/geoip/docs/databases/anonymous-ip>`_.

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   ``<db_type>.total``, Counter, Total number of lookups performed for a given geolocation database file.
   ``<db_type>.hit``, Counter, Total number of successful lookups (with non empty lookup result) performed for a given geolocation database file.
   ``<db_type>.lookup_error``, Counter, Total number of errors that occured during lookups for a given geolocation database file.
   ``<db_type>.db_reload_success``, Counter, Total number of times when the geolocation database file was reloaded successfully.
   ``<db_type>.db_reload_error``, Counter, Total number of times when the geolocation database file failed to reload.


