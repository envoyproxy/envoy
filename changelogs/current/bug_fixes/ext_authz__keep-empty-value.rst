Fixed a bug where ``keep_empty_value`` in ``HeaderValueOption`` was not respected by
the ext_authz filter. Empty-valued headers are now dropped by default. This behavior
can be reverted by setting the runtime guard
``envoy.reloadable_features.ext_authz_respect_keep_empty_value`` to ``false``.
