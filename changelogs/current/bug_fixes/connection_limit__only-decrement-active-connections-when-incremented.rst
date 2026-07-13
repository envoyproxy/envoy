Fixed a bug in the ``connection_limit`` network filter where the connection accounting (the
``active_connections`` gauge and the internal connection counter) was decremented on connection
close even when it was never incremented, e.g. when the filter was runtime disabled or when an
earlier filter in the chain stopped iteration before this filter ran. The resulting unsigned
underflow caused the gauge to report huge values and could cause all subsequent connections to be
wrongly rejected as over the limit. See https://github.com/envoyproxy/envoy/issues/22127.
