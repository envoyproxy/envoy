The GOAWAY load shed point is fixed to use a graceful two-phase shutdown sequence, to avoid
risk to client traffic. This behavioral change can be temporarily reverted by setting runtime
guard ``envoy.reloadable_features.http2_fix_goaway_loadshed_point`` to ``false``.
