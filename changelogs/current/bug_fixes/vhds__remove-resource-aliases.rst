Fixed a bug where removing a delta xDS or VHDS resource did not remove its associated aliases from
watch interest, requested resource state, or the VHDS answered alias cache, preventing subsequent
on-demand requests for those aliases from re-subscribing. This behavior can be temporarily reverted
by setting the runtime guard ``envoy.reloadable_features.vhds_remove_resource_aliases`` to
``false``.
