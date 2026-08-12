Added propagation of downstream TCP RST to upstream for direct TCP proxy connections on Linux when the
detected close type is ``RemoteReset``. This behavioral change can be temporarily reverted by
setting runtime guard ``envoy.reloadable_features.propagate_downstream_rst_to_upstream`` to ``false``.
