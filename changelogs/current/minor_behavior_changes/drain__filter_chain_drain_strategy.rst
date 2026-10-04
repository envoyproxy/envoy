The connections of a draining filter chain (an in-place listener filter chain update, or a filter
chain removal) are now notified with the configured drain strategy (``--drain-strategy``) instead
of always being notified with ``immediate``. With the default ``gradual`` strategy these
connections are now drain-closed by ramping up over the drain window rather than at the first
opportunity. This behavioral change can be reverted by setting the runtime guard
``envoy.reloadable_features.filter_chain_drain_uses_configured_strategy`` to ``false``.
