Configuring the :ref:`envoy.watchdog.backtrace_action
<envoy_v3_api_msg_extensions.watchdog.backtrace_action.v3.BacktraceActionConfig>` now causes Envoy to
install a process-wide ``SIGUSR2`` signal handler and to send ``SIGUSR2`` to stuck threads in order
to capture their backtraces. Deployments that rely on ``SIGUSR2`` for other purposes should avoid
enabling this action.
