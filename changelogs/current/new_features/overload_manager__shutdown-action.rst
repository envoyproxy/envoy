Added the ``envoy.overload_actions.shutdown`` overload action. When the action stays saturated for
:ref:`saturation_duration <envoy_v3_api_field_config.overload.v3.ShutdownConfig.saturation_duration>`
without interruption, Envoy drains and exits so that a supervising process can restart it. This
gives a way out of overload conditions that never clear on their own, such as a memory leak or heap
fragmentation that keeps memory pressure above the threshold at which Envoy stops accepting
requests. See :ref:`the docs <config_overload_manager_shutdown>` for details.
