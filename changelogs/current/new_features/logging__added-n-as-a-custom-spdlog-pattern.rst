Added ``%N`` as a custom spdlog pattern flag that emits the Envoy version string. It can be
used in the ``--log-format`` CLI flag or the bootstrap ``application_log_config.log_format``
to include the running version in every log line, e.g. ``--log-format "[%N][%l] %v"``.
