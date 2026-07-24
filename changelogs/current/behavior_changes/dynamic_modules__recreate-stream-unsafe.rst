Made the Rust dynamic module SDK ``EnvoyHttpFilter::recreate_stream`` an ``unsafe`` method, since a
successful call synchronously destroys the current filter chain — freeing the filter that backs
``self`` — before it returns, so any subsequent use of ``self`` is a use-after-free. A new safe
``EnvoyHttpFilter::request_stream_recreation`` performs the recreation from the SDK event loop after
the filter hook returns, when no reference to the filter remains. Modules that call
``recreate_stream`` directly must now wrap it in ``unsafe`` or switch to
``request_stream_recreation``.
