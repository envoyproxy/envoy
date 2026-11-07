Added the
:ref:`allow_missing_database
<envoy_v3_api_field_extensions.geoip_providers.maxmind.v3.MaxMindConfig.allow_missing_database>`
option to let the MaxMind geolocation provider start when a configured database is unavailable and
hot-load the database when it appears. The default behavior still rejects the configuration.
