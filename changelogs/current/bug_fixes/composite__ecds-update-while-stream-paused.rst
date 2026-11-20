Fixed a use-after-free in the :ref:`composite <envoy_v3_api_msg_extensions.filters.http.composite.v3.Composite>`
filter when it is configured through
:ref:`config_discovery <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.config_discovery>`
and an ECDS update replaces its configuration while a stream is paused in an earlier filter. The
filter instances created from the previous configuration now share ownership of the filter stats,
and the match tree and named filter chains they hold are released on the main thread, so that the
delegated filter configurations are not destroyed on a worker thread.
