.. _config_overload_manager:

Overload manager
================

The :ref:`overload manager <arch_overview_overload_manager>` is configured in the Boostrap
:ref:`overload_manager <envoy_api_field_config.bootstrap.v2.Bootstrap.overload_manager>`
field.

Resource monitors
-----------------

The overload manager uses Envoy's :ref:`extension <extending>` framework for defining
resource monitors. Envoy's builtin resource monitors are listed
:ref:`here <config_resource_monitors>`.

Statistics
----------

Each configured resource monitor has a statistics tree rooted at *overload.<name>.*
with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  pressure, Gauge, Resource pressure as a percent
  failed_updates, Counter, Total failed attempts to update the resource pressure
  skipped_updates, Counter, Total skipped attempts to update the resource pressure due to a pending update

Each configured overload action has a statistics tree rooted at *overload.<name>.*
with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  active, Gauge, "Active state of the action (0=inactive, 1=active)"
