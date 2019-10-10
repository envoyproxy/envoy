.. _config_http_filters_adaptive_concurrency:

Adaptive Concurrency
====================

.. attention::

  The adaptive concurrency filter is experimental and is currently under active development.

Overview
--------


Configuration
-------------

Concurrency Controllers
-----------------------

Gradient Controller
^^^^^^^^^^^^^^^^^^^


Example Configuration
---------------------

Runtime
-------

The adaptive concurrency filter supports the following runtime settings:

adaptive_concurrency.enabled
    Whether the adaptive concurrency filter will use the concurrency controller for forwarding
    decisions. If set to `false`, the filter will be a no-op. Default is `true`.

adaptive_concurrency.gradient_controller.min_rtt_calc_interval_ms
    

adaptive_concurrency.gradient_controller.sample_rtt_calc_interval_ms

adaptive_concurrency.gradient_controller.max_concurrency_limit

adaptive_concurrency.gradient_controller.min_rtt_aggregate_request_count

adaptive_concurrency.gradient_controller.max_gradient

adaptive_concurrency.gradient_controller.sample_aggregate_percentile

adaptive_concurrency.gradient_controller.jitter

Statistics
----------

