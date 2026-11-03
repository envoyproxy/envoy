A TCP listener that has filter chains from the Filter Chain Discovery Service (FCDS) and no
:ref:`filter_chains <envoy_v3_api_field_config.listener.v3.Listener.filter_chains>` is now updated
in place in the same cases as the other listeners, instead of always being drained and replaced.
The connections on the FCDS filter chains that the update keeps remain open. This behavior can be
temporarily reverted by setting runtime guard
``envoy.reloadable_features.listener_in_place_address_update`` to ``false``. An in place update of
any listener also drains its connections on the FCDS filter chains that the update drops. When the
``envoy.reloadable_features.use_connection_event_drain`` runtime guard is disabled, these
connections are only closed once the drain completes.
