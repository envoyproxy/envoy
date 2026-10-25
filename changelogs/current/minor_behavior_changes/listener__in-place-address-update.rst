A TCP listener update that adds or removes addresses, and otherwise changes at most the filter
chains, no longer drains the entire listener. The listener keeps accepting connections on the
remaining addresses, and the connections accepted on the removed addresses are drained along with
those of the removed filter chains. The new
:ref:`total_listener_addresses_draining <config_listener_manager_stats>` listener manager gauge
counts the removed addresses until their drain completes. Such an update of a listener with
:ref:`enable_reuse_port <envoy_v3_api_field_config.listener.v3.Listener.enable_reuse_port>`
disabled is also no longer rejected with a duplicate address error. If the listener fails to listen
on an added address, it drops the update and keeps serving its addresses instead of removing the
listener. See :ref:`filter chain only update <filter_chain_only_update>` for the requirements. This
behavior can be temporarily reverted by setting runtime guard
``envoy.reloadable_features.listener_in_place_address_update`` to ``false``.
