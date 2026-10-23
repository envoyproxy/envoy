The dynamic forward proxy cluster now replaces a host with a new host instance when DNS
re-resolution changes its address, publishing the change as a regular cluster membership update
(old host removed, new host added). Connection pools bound to the old address are therefore
drained, instead of existing connections continuing to use the stale address indefinitely. This
change can be reverted by setting the runtime guard
``envoy.reloadable_features.dfp_cluster_replace_host_on_address_change`` to ``false``, which
restores the previous behavior of mutating the existing host's address in place.
