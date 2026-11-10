Added a dynamic modules resource monitor extension (``envoy.resource_monitors.dynamic_modules``)
that lets a dynamic module report the pressure of an overload manager resource at each refresh, or
later from any thread through a scheduler. The module creates the scheduler with
``envoy_dynamic_module_callback_resource_monitor_config_scheduler_new``, reports through it with
``envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_resource_usage`` and
``envoy_dynamic_module_callback_resource_monitor_config_scheduler_report_failure``, and deletes it
with ``envoy_dynamic_module_callback_resource_monitor_config_scheduler_delete``. The Rust SDK
exposes this through the ``resource_monitor:`` arm of ``declare_all_init_functions!``,
``ResourceMonitor::update_resource_usage``, ``EnvoyResourceMonitorConfig::new_config_scheduler``,
and the ``report_resource_usage`` and ``report_failure`` methods of
``EnvoyResourceMonitorConfigScheduler``.
A module can also enable synchronous feedback with
``envoy_dynamic_module_callback_resource_monitor_config_enable_synchronous_feedback``, so that the
load shed points with a trigger on the resource ask it for the pressure each time they decide
whether to shed load, and tell it about the load they accept. The Rust SDK exposes this through the
``synchronous_feedback_resource_monitor:`` arm of ``declare_all_init_functions!``,
``SynchronousFeedbackResourceMonitor::resource_usage`` and
``SynchronousFeedbackResourceMonitor::on_load_accepted``.
