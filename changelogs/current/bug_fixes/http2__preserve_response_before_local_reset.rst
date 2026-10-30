http2: wait for a fully submitted response to finish encoding before resetting an open request
  when upstream half-close is disabled. Previously a flow-controlled response could be truncated
  with an INTERNAL_ERROR reset. This behavior can be disabled by setting the runtime guard
  ``envoy.reloadable_features.http2_defer_reset_until_response_complete`` to false. The existing
  stream flush timeout bounds the wait; a configured timeout of zero continues to disable it.
