.. _config_overload_manager:

Overload manager
================

The :ref:`overload manager <arch_overview_overload_manager>` is configured in the Bootstrap
:ref:`overload_manager <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.overload_manager>`
field.

An example configuration of the overload manager is shown below. It shows a
configuration to drain HTTP/X connections when heap memory usage reaches 95%
and to stop accepting requests when heap memory usage reaches 99%.

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
    - Envoy will drain HTTP/2 and HTTP/3 connections using ``GOAWAY`` with a
      drain grace period. For HTTP/1, Envoy will set a drain timer to close the
      more idle recently used connections.

  * - envoy.overload_actions.stop_accepting_connections
    - Envoy will stop accepting new network connections on its configured listeners

  * - envoy.overload_actions.reject_incoming_connections
    - Envoy will reject incoming connections on its configured listeners without processing any data

  * - envoy.overload_actions.shrink_heap
    - Envoy will periodically try to shrink the heap by releasing free memory to the system

  * - envoy.overload_actions.reduce_timeouts
    - Envoy will reduce the waiting period for a configured set of timeouts. See
      :ref:`below <config_overload_manager_reducing_timeouts>` for details on configuration.

  * - envoy.overload_actions.reset_high_memory_stream
    - Envoy will reset expensive streams to terminate them. See
      :ref:`below <config_overload_manager_reset_streams>` for details on configuration.

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

.. _config_overload_manager_limiting_connections:

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
Listeners can opt out of this global connection limit by setting
:ref:`Listener.ignore_global_conn_limit <envoy_v3_api_field_config.listener.v3.Listener.ignore_global_conn_limit>`
to true. Similarly, you can opt out the admin listener by setting
:ref:`Admin.ignore_global_conn_limit <envoy_v3_api_field_config.bootstrap.v3.Admin.ignore_global_conn_limit>`.
You may want to opt out a listener to be able to probe Envoy or collect stats while it is otherwise at its
connection limit. Note that connections to listeners that opt out are still tracked and count towards the
global limit.

If it is desired to only limit the number of downstream connections for a particular listener,
per-listener limits can be set via the :ref:`listener configuration <config_listeners>`.

One may simultaneously specify both per-listener and global downstream connection limits and the
conditions will be enforced independently. For instance, if it is known that a particular listener
should have a smaller number of open connections than others, one may specify a smaller connection
limit for that specific listener and allow the global limit to enforce resource utilization among
all listeners.

An example configuration can be found in the :ref:`edge best practices document <best_practices_edge>`.

.. _config_overload_manager_reset_streams:

Reset Streams
^^^^^^^^^^^^^

.. warning::
   Resetting streams via an overload action currently only works with HTTP2.

The ``envoy.overload_actions.reset_high_memory_stream`` overload action will reset
expensive streams. This requires :ref:`minimum_account_to_track_power_of_two
<envoy_v3_api_field_config.overload.v3.BufferFactoryConfig.minimum_account_to_track_power_of_two>` to be
configured via :ref:`buffer_factory_config
<envoy_v3_api_field_config.overload.v3.OverloadManager.buffer_factory_config>`.
To understand the memory class scheme in detail see :ref:`minimum_account_to_track_power_of_two
<envoy_v3_api_field_config.overload.v3.BufferFactoryConfig.minimum_account_to_track_power_of_two>`

As an example, here is a partial Overload Manager configuration with minimum
threshold for tracking and a single overload action entry that resets streams:

.. code-block:: yaml

  buffer_factory_config:
    minimum_account_to_track_power_of_two: 20
  actions:
    name: "envoy.overload_actions.reset_high_memory_stream"
    triggers:
      - name: "envoy.resource_monitors.fixed_heap"
        scaled:
          scaling_threshold: 0.85
          saturation_threshold: 0.95
  ...

We will only track streams using >=
:math:`2^{minimum\_account\_to\_track\_power\_of\_two}` worth of allocated memory in
buffers. In this case, by setting the :ref:`minimum_account_to_track_power_of_two
<envoy_v3_api_field_config.overload.v3.BufferFactoryConfig.minimum_account_to_track_power_of_two>`
to 20 we will track streams using >= 1MiB since :math:`2^{20}` is 1MiB. Streams
using >= 1MiB will be classified into 8 power of two sized buckets. Currently,
the number of buckets is hardcoded to 8.  For this example, the buckets are as
follows:

.. list-table::
  :header-rows: 1
  :widths: 1, 2

  * - Bucket index
    - Contains streams using
  * - 0
    - [1MiB,2MiB)
  * - 1
    - [2MiB,4MiB)
  * - 2
    - [4MiB,8MiB)
  * - 3
    - [8MiB,16MiB)
  * - 4
    - [16MiB,32MiB)
  * - 5
    - [32MiB,64MiB)
  * - 6
    - [64MiB,128MiB)
  * - 7
    - >= 128MiB

The above configuration also configures the overload manager to reset our tracked
streams based on heap usage as a trigger. When the heap usage is less than 85%,
no streams will be reset.  When heap usage is at or above 85%, we start to
reset buckets according to the strategy described below. When the heap
usage is at 95% all streams using >= 1MiB memory are eligible for reset.
This overload action will reset up to 50 streams (this is a hardcoded limit)
per worker everytime the action is invoked. This is both to reduce the amount
of streams that end up getting reset and to prevent the worker thread from
locking up and triggering the Watchdog system.

Given that there are only 8 buckets, we partition the space with a gradation of
:math:`gradation = (saturation\_threshold - scaling\_threshold)/8`. Hence at 85%
heap usage we reset streams in the last bucket e.g. those using ``>= 128MiB``. At
:math:`85\% + 1 * gradation` heap usage we reset streams in the last two buckets
e.g. those using ``>= 64MiB``, prioritizing the streams in the last bucket since
there's a hard limit on the number of streams we can reset per invokation.
At :math:`85\% + 2 * gradation` heap usage we reset streams in the last three
buckets e.g. those using ``>= 32MiB``. And so forth as the heap usage is higher.

It's expected that the first few gradations shouldn't trigger anything, unless
there's something seriously wrong e.g. in this example streams using ``>=
128MiB`` in buffers.


Statistics
----------

Each configured resource monitor has a statistics tree rooted at ``overload.<name>.``
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
