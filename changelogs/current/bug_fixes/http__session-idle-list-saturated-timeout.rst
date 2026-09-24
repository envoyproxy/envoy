Enforced a 10-second minimum idle duration before terminating idle HTTP/3 sessions when the
``envoy.overload_actions.close_idle_http_connections`` overload action is saturated. This behavioral
change can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.session_idle_list_min_timeout_when_saturated`` to ``false``.
