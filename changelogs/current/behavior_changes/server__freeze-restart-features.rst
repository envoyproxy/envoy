:ref:`Restart features <arch_overview_runtime_restart_features>` (``envoy.restart_features.*``)
are now only set by the runtime layers present at startup. Later changes from RTDS or disk layers
are ignored and logged as errors, and ``/runtime_modify`` rejects them with ``400 Bad Request``.
Restart features set through RTDS need to move to a static or disk layer, or the freeze can be
reverted by setting ``envoy.restart_features.freeze_restart_features`` to ``false`` in such a layer.
