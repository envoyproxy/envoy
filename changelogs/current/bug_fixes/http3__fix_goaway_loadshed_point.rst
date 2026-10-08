Fixed a bug where HTTP/3 load shed points ``envoy.load_shed_points.http3_server_go_away_on_dispatch``
and ``envoy.load_shed_points.http3_server_go_away_and_close_on_dispatch`` were evaluated on every UDP
packet rather than only on new stream creation. This behavioral change can be reverted by setting
the runtime guard ``envoy.reloadable_features.http3_fix_goaway_loadshed_point`` to ``false``.
