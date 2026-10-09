Fixed a bug in the :ref:`ip_tagging <config_http_filters_ip_tagging>` filter where stats
could be published under another listener's stats prefix.
Filter configs that load tags from the same :ref:`ip_tags_datasource
<envoy_v3_api_field_extensions.filters.http.ip_tagging.v3.IPTagging.ip_tags_datasource>` file
share same cached provider and could end up publishing stats under the wrong listener's
stats prefix.
