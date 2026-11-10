Added :ref:`filter_state <envoy_v3_api_field_extensions.filters.http.set_metadata.v3.Metadata.filter_state>`
to the :ref:`set_metadata filter <config_http_filters_set_metadata>`, which copies a serializable filter
state object into typed dynamic metadata. Added the ``filter_state_not_found`` and
``filter_state_not_serializable`` counters.
