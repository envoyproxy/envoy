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

Dynamic Client IP Override
--------------------------

By default, the filter uses the downstream connection's remote address for geolocation lookups.
However, the client IP can be dynamically overridden by setting
:ref:`client_ip_filter_state_config <envoy_v3_api_field_extensions.filters.network.geoip.v3.Geoip.client_ip_filter_state_config>`.
When configured, the filter will first attempt to read the client IP address from the specified
filter state key. If the filter state object is not found or contains an invalid IP address,
the filter falls back to using the downstream connection source address.

This is useful when a preceding filter (such as the PROXY protocol listener filter or a custom
network filter) has extracted the real client IP address from a protocol header and stored it in filter
state. For example, you can use the :ref:`set_filter_state network filter
<config_network_filters_set_filter_state>` to set the client IP from PROXY protocol TLVs.

Example configuration with dynamic client IP override:

.. code-block:: yaml

  filter_chains:
  - filters:
    # First, set the client IP in filter state.
    - name: envoy.filters.network.set_filter_state
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
        on_new_connection:
        - object_key: my.client.ip
          format_string:
            text_format_source:
              inline_string: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    # Then use the geoip filter with the filter state key.
    - name: envoy.filters.network.geoip
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.geoip.v3.Geoip
        client_ip_filter_state_config:
          filter_state_key: "my.client.ip"
        provider:
          # ... provider configuration ...

The filter state object must implement the ``Router::StringAccessor`` interface and contain a
valid IPv4 or IPv6 address string.

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

The MaxMind provider emits additional statistics in the ``maxmind.`` namespace per database type.
See the :ref:`MaxMind provider documentation <envoy_v3_api_msg_extensions.geoip_providers.maxmind.v3.MaxMindConfig>`
for details.
