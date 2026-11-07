Added support for using the :ref:`set filter state HTTP filter <config_http_filters_set_filter_state>`
as an upstream HTTP filter, so filter state can be set per upstream attempt from the selected host,
for example with ``%UPSTREAM_METADATA(...)%``.
