Fixed a use-after-free when a listener filter calls ``continueFilterChain`` from inside its own
``onAccept``, ``onData``, or ``onClose`` hook. The re-entrant continuation cleared the accept filter
list under the running filter, leaving a dangling iterator and a null socket. The continuation is now
deferred and applied after the hook returns. This behavior is guarded by
``envoy.reloadable_features.listener_filter_reentrant_continue_guard``.
