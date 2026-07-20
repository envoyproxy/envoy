Added support for populating client latitude and longitude from the Maxmind city database via the
new :ref:`lat <envoy_v3_api_field_extensions.geoip_providers.common.v3.CommonGeoipProviderConfig.GeolocationFieldKeys.lat>`
and :ref:`lon <envoy_v3_api_field_extensions.geoip_providers.common.v3.CommonGeoipProviderConfig.GeolocationFieldKeys.lon>`
geolocation field keys. Coordinates are read from the ``location.latitude`` and ``location.longitude``
``double`` fields and rendered as decimal-degree strings (e.g. ``58.4167``).
