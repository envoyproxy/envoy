.. _operations_performance:

Performance
===========

Envoy is architected to optimize scalability and resource utilization by running an event loop on a
:ref:`small number of threads <arch_overview_threading>`. The "main" thread is responsible for
control plane processing, and each "worker" thread handles a portion of the data plane processing.
Envoy exposes two statistics to monitor performance of the event loops on all these threads.

* **Loop duration:** Some amount of processing is done on each iteration of the event loop. This
  amount will naturally vary with changes in load. However, if one or more threads have an unusually
  long-tailed loop duration, it may indicate a performance issue. For example, work might not be
  distributed fairly across the worker threads, or there may be a long blocking operation in an
  extension that's impeding progress.

* **Poll delay:** On each iteration of the event loop, the event dispatcher polls for I/O events
  and "wakes up" either when some I/O events are ready to be processed or when a timeout fires,
  whichever occurs first. In the case of a timeout, we can measure the difference between the
  expected wakeup time and the actual wakeup time after polling; this difference is called the "poll
  delay." It's normal to see some small poll delay, usually equal to the kernel scheduler's "time
  slice" or "quantum"---this depends on the specific operating system on which Envoy is
  running---but if this number elevates substantially above its normal observed baseline, it likely
  indicates kernel scheduler delays.

These statistics can be enabled by setting :ref:`enable_dispatcher_stats <envoy_api_field_config.bootstrap.v2.Bootstrap.enable_dispatcher_stats>`
to true.

.. warning::

  Note that enabling dispatcher stats records a value for each iteration of the event loop on every
  thread. This should normally be minimal overhead, but when using
  :ref:`statsd <envoy_api_msg_config.metrics.v2.StatsdSink>`, it will send each observed value over
  the wire individually because the statsd protocol doesn't have any way to represent a histogram
  summary. Be aware that this can be a very large volume of data.

Statistics
----------

The event dispatcher for the main thread has a statistics tree rooted at *server.dispatcher.*, and
the event dispatcher for each worker thread has a statistics tree rooted at
*listener_manager.worker_<id>.dispatcher.*, each with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  loop_duration_us, Histogram, Event loop durations in microseconds
  poll_delay_us, Histogram, Polling delays in microseconds

Note that any auxiliary threads are not included here.
