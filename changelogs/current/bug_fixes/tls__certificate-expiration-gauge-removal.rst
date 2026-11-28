Fixed the ``ssl.certificate.<name>.expiration_unix_time_seconds`` gauges remaining exported after
the owning TLS context was destroyed, for example when an SDS secret was removed and its listener
context drained. The gauges are now created in a scope owned by the TLS context and are removed
with it.
