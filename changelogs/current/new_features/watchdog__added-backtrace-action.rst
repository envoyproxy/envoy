Added :ref:`envoy.watchdog.backtrace_action<envoy_v3_api_msg_extensions.watchdog.backtrace_action.v3.BacktraceActionConfig>`, a
new watchdog action that logs a stack backtrace of stuck threads when the watchdog fires. A configurable cooldown prevents
duplicate backtraces for the same thread.
