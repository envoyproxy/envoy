.. _config_network_filters_geoip:

IP Geolocation
==============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.geoip.v3.Geoip``.
* :ref:`Network filter v3 API reference <envoy_v3_api_msg_extensions.filters.network.geoip.v3.Geoip>`

The IP geolocation network filter performs geolocation lookups on incoming connections and stores
the results in the connection's filter state under the well-known key ``envoy.geoip``.
The filter uses the client's remote IP address to determine geographic information such as country,
city, region, and ASN using a configured geolocation provider.

.. tip::

  This filter is useful for logging geolocation data, making routing decisions based on client
  location, or passing location information to upstream services.

.. note::

  The geolocation filter and providers are not yet supported on Windows.

.. _config_network_filters_geoip_providers:

Geolocation Providers
---------------------

The filter requires a geolocation provider to perform the actual IP lookups. Currently, only the
MaxMind provider is supported.

* :ref:`MaxMind provider configuration <envoy_v3_api_msg_extensions.geoip_providers.maxmind.v3.MaxMindConfig>`

The provider configuration specifies which geolocation fields to look up and what keys to use when
storing the results. Use the ``geo_field_keys`` field in the provider configuration to define the
field names for each geolocation attribute.

Example
-------

A sample filter configuration:

.. literalinclude:: _include/geoip-network-filter.yaml
    :language: yaml
    :linenos:
    :caption: geoip-network-filter.yaml

Accessing Geolocation Data
--------------------------

The filter stores geolocation results in a ``GeoipInfo`` object in the connection's filter state
under the well-known key ``envoy.geoip``. See :ref:`well known filter state
<well_known_filter_state>` for details.

The data can be accessed in several ways:

**Access Logs**

Use the ``FILTER_STATE`` format specifier:

.. code-block:: text

  # Get all geo data as JSON
  %FILTER_STATE(envoy.geoip:PLAIN)%

  # Get a specific field
  %FILTER_STATE(envoy.geoip:FIELD:country)%

**Other Filters**

Downstream filters can access the ``GeoipInfo`` object from the connection's filter state using
the well-known key ``envoy.geoip``.

Statistics
----------

The filter outputs statistics in the ``geoip.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  total, Counter, Total number of connections processed by the filter.

The MaxMind provider emits additional statistics in the ``<stat_prefix>.maxmind.`` namespace per database type.
Database type can be one of `city_db <https://www.maxmind.com/en/geoip2-city>`_,
`country_db <https://www.maxmind.com/en/geoip2-country>`_, `isp_db <https://www.maxmind.com/en/geoip2-isp-database>`_, `anon_db <https://dev.maxmind.com/geoip/docs/databases/anonymous-ip>`_, `asn_db <https://www.maxmind.com/en/geoip2-asn-database>`_.

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   ``<db_type>.total``, Counter, Total number of lookups performed for a given geolocation database file.
   ``<db_type>.hit``, Counter, Total number of successful lookups (with non empty lookup result) performed for a given geolocation database file.
   ``<db_type>.lookup_error``, Counter, Total number of errors that occured during lookups for a given geolocation database file.
   ``<db_type>.db_reload_success``, Counter, Total number of times when the geolocation database file was reloaded successfully.
   ``<db_type>.db_reload_error``, Counter, Total number of times when the geolocation database file failed to reload.
   ``<db_type>.db_build_epoch``, Gauge, The build timestamp of the geolocation database file represented as a Unix epoch value.
