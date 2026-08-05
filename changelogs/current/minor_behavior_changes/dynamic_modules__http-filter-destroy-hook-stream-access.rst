``envoy_dynamic_module_on_http_filter_destroy`` now runs after the HTTP stream is gone, so the
callbacks that need it, for example the ones reading the headers, the stream info or the buffered
bodies, are no-ops. Modules that did end of stream bookkeeping from the destroy hook should do it
from ``envoy_dynamic_module_on_http_filter_stream_complete`` instead.
