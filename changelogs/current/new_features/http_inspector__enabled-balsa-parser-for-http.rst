Enabled Balsa parser for HTTP inspector by default. This behavior can be temporarily
reverted by setting the runtime guard ``envoy.reloadable_features.http_inspector_use_balsa_parser``
to ``false``. This runtime guard will be removed in a future release of Envoy.
