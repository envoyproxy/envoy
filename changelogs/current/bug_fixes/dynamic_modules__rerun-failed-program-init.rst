Fixed the use of a dynamic module whose ``envoy_dynamic_module_on_program_init`` failed. A module
that stayed loaded after the failure, for example with ``do_not_close``, was returned by its next
load without calling the init function again. Such a load now calls the init function again, and
fails again if the init function fails. This behavioral change can be temporarily reverted by
setting runtime guard ``envoy.reloadable_features.dynamic_modules_rerun_failed_program_init`` to
``false``.
