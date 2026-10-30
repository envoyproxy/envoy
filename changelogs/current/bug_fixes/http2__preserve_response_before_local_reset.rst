http2: preserve fully submitted responses before normal local resets of open downstream requests
  when independent upstream half-close is disabled. Previously, flow-controlled responses could
  be truncated with an INTERNAL_ERROR reset. CONNECT requests and established tunnels retain
  their existing behavior, and explicit aborts are not delayed by this change.
  The new response-draining behavior is enabled by default and can be disabled by setting
  ``envoy.reloadable_features.http2_defer_reset_until_response_complete`` to false.
  The existing stream flush timeout limits the wait; a configured timeout of zero disables
  that timeout.
