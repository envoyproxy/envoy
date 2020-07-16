.. _config_overload_manager:

Overload manager
================

The :ref:`overload manager <arch_overview_overload_manager>` is configured in the Bootstrap
:ref:`overload_manager <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.overload_manager>`
field.

An example configuration of the overload manager is shown below. It shows a configuration to
disable HTTP/1.x keepalive when heap memory usage reaches 95% and to stop accepting
requests when heap memory usage reaches 99%.

.. code-block:: yaml

   refresh_interval:
     seconds: 0
     nanos: 250000000
   resource_monitors:
     - name: "envoy.resource_monitors.fixed_heap"
       typed_config:
         "@type": type.googleapis.com/envoy.config.resource_monitor.fixed_heap.v2alpha.FixedHeapConfig
         max_heap_size_bytes: 2147483648
   actions:
     - name: "envoy.overload_actions.disable_http_keepalive"
       triggers:
         - name: "envoy.resource_monitors.fixed_heap"
           threshold:
             value: 0.95
     - name: "envoy.overload_actions.stop_accepting_requests"
       triggers:
         - name: "envoy.resource_monitors.fixed_heap"
           threshold:
             value: 0.99

Resource monitors
-----------------

The overload manager uses Envoy's :ref:`extension <extending>` framework for defining
resource monitors. Envoy's builtin resource monitors are listed
:ref:`here <config_resource_monitors>`.

Overload actions
----------------

The following overload actions are supported:

.. csv-table::
  :header: Name, Description
  :widths: 1, 2

  envoy.overload_actions.stop_accepting_requests, Envoy will immediately respond with a 503 response code to new requests
  envoy.overload_actions.disable_http_keepalive, Envoy will disable keepalive on HTTP/1.x responses
  envoy.overload_actions.stop_accepting_connections, Envoy will stop accepting new network connections on its configured listeners
  envoy.overload_actions.shrink_heap, Envoy will periodically try to shrink the heap by releasing free memory to the system

Limiting Active Connections
---------------------------

Currently, the only supported way to limit the total number of active connections allowed across all
listeners is via specifying an integer through the runtime key
``overload.global_downstream_max_connections``. The connection limit is recommended to be less than
half of the system's file descriptor limit, to account for upstream connections, files, and other
usage of file descriptors.
If the value is unspecified, there is no global limit on the number of active downstream connections
and Envoy will emit a warning indicating this at startup. To disable the warning without setting a
limit on the number of active downstream connections, the runtime value may be set to a very large
limit (~2e9).

If it is desired to only limit the number of downstream connections for a particular listener,
per-listener limits can be set via the :ref:`listener configuration <config_listeners>`.

One may simultaneously specify both per-listener and global downstream connection limits and the
conditions will be enforced independently. For instance, if it is known that a particular listener
should have a smaller number of open connections than others, one may specify a smaller connection
limit for that specific listener and allow the global limit to enforce resource utilization among
all listeners.

An example configuration can be found in the :ref:`edge best practices document <best_practices_edge>`.

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
