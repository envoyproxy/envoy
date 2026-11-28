Fixed a crash and use-after-free in the ``/reverse_tunnel/tunnels`` admin handler when a reverse
tunnel listener handle is torn down while accepted downstream tunnel handles are still draining.
``DownstreamReverseConnectionIOHandle`` now caches its source identifiers at construction and holds a
weak reference to its worker-local registry so detached tunnels still report their identifiers while
draining and unregister on destruction. Also added a bounds check on the worker index in
``ReverseTunnelInitiatorExtension::activeTunnels()``.
