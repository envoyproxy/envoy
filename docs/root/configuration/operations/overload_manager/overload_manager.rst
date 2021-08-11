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
         "@type": type.googleapis.com/envoy.extensions.resource_monitors.fixed_heap.v3.FixedHeapConfig
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
:ref:`here <v3_config_resource_monitors>`.

.. _config_overload_manager_triggers:

Triggers
--------

Triggers connect resource monitors to actions. There are two types of triggers supported:

.. list-table::
  :header-rows: 1
  :widths: 1, 2

  * - Type
    - Description
  * - :ref:`threshold <envoy_v3_api_msg_config.overload.v3.ThresholdTrigger>`
    - Sets the action state to 1 (= *saturated*) when the resource pressure is above a threshold, and to 0 otherwise.
  * - :ref:`scaled <envoy_v3_api_msg_config.overload.v3.ScaledTrigger>`
    - Sets the action state to 0 when the resource pressure is below the
      :ref:`scaling_threshold <envoy_v3_api_field_config.overload.v3.ScaledTrigger.scaling_threshold>`,
      ``(pressure - scaling_threshold)/(saturation_threshold - scaling_threshold)`` when
      ``scaling_threshold < pressure < saturation_threshold``, and to 1 (*saturated*) when the
      pressure is above the
      :ref:`saturation_threshold <envoy_v3_api_field_config.overload.v3.ScaledTrigger.saturation_threshold>`."

.. _config_overload_manager_overload_actions:

Overload actions
----------------

The following overload actions are supported:

.. list-table::
  :header-rows: 1
  :widths: 1, 2

  * - Name
    - Description

  * - envoy.overload_actions.stop_accepting_requests
    - Envoy will immediately respond with a 503 response code to new requests

  * - envoy.overload_actions.disable_http_keepalive
    - Envoy will stop accepting streams on incoming HTTP connections

  * - envoy.overload_actions.stop_accepting_connections
    - Envoy will stop accepting new network connections on its configured listeners

  * - envoy.overload_actions.reject_incoming_connections
    - Envoy will reject incoming connections on its configured listeners without processing any data

  * - envoy.overload_actions.shrink_heap
    - Envoy will periodically try to shrink the heap by releasing free memory to the system

  * - envoy.overload_actions.reduce_timeouts
    - Envoy will reduce the waiting period for a configured set of timeouts. See
      :ref:`below <config_overload_manager_reducing_timeouts>` for details on configuration.

.. _config_overload_manager_reducing_timeouts:

Reducing timeouts
^^^^^^^^^^^^^^^^^

The ``envoy.overload_actions.reduce_timeouts`` overload action will reduce the amount of time Envoy
will spend waiting for some interactions to finish in response to resource pressure. The amount of
reduction can be configured per timeout type by specifying the minimum timer value to use when the
triggering resource monitor detects saturation. The minimum value for each timeout can be specified
either by providing a scale factor to apply to the configured maximum, or as a concrete duration
value.

As an example, here is a single overload action entry that enables timeout reduction:

.. code-block:: yaml

  name: "envoy.overload_actions.reduce_timeouts"
  triggers:
    - name: "envoy.resource_monitors.fixed_heap"
      scaled:
        scaling_threshold: 0.85
        saturation_threshold: 0.95
  typed_config:
    "@type": type.googleapis.com/envoy.config.overload.v3.ScaleTimersOverloadActionConfig
    timer_scale_factors:
      - timer: HTTP_DOWNSTREAM_CONNECTION_IDLE
        min_timeout: 2s

It configures the overload manager to change the amount of time that HTTP connections are allowed
to remain idle before being closed in response to heap size. When the heap usage is less than 85%,
idle connections will time out at their usual time, which is configured through
:ref:`HttpConnectionManager.common_http_protocol_options.idle_timeout <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.idle_timeout>`.
When the heap usage is at or above 95%, idle connections will be closed after the specified
``min_timeout``, here 2 seconds. If the heap usage is between 85% and 95%, the idle connection timeout
will vary between those two based on the formula for the :ref:`scaled trigger <config_overload_manager_triggers>`
So if ``RouteAction.idle_timeout = 600 seconds`` and heap usage is at 92%, idle connections will time
out after :math:`2s + (600s - 2s) \cdot (95\% - 92\%) / (95\% - 85\%) = 181.4s`.

Note in the example that the minimum idle time is specified as an absolute duration. If, instead,
``min_timeout: 2s`` were to be replaced with ``min_scale: { value: 10 }``, the minimum timer value
would be computed based on the maximum (specified elsewhere). So if ``idle_timeout`` is
again 600 seconds, then the minimum timer value would be :math:`10\% \cdot 600s = 60s`.

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

  active, Gauge, "Active state of the action (0=scaling, 1=saturated)"
  scale_percent, Gauge, "Scaled value of the action as a percent (0-99=scaling, 100=saturated)"
