Coalesced Scoped RDS (SRDS) scope removals and additions into a single worker-thread configuration
update, validated empty ``route_configuration_name`` fields up front when
``envoy.reloadable_features.srds_partial_acceptance`` is ``false`` so rejected pushes do not
partially mutate subscription state, and retained replaced dynamic RDS providers until the update
batch completes so other scopes in the same batch can reuse warm RDS subscriptions. This behavioral
change can be temporarily reverted by setting runtime guard
``envoy.reloadable_features.srds_fast_config_update`` to ``false``.
