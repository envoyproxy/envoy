.. _faq_overview:

FAQ
===

Build
-----

.. toctree::
  :maxdepth: 2

  build/binaries
  build/boringssl
  build/speed

API
---
.. toctree::
  :maxdepth: 2

  api/envoy_v3
  api/extensions
  api/package_naming
  api/why_versioning
  api/incremental

.. _faq_overview_debug:

Debugging
---------
.. toctree::
  :maxdepth: 2

  debugging/why_is_envoy_sending_internal_responses
  debugging/why_is_envoy_sending_http2_resets
  debugging/why_is_envoy_404ing_connect_requests
  debugging/why_is_envoy_sending_413s
  debugging/why_is_my_route_not_found
  debugging/xfp_vs_scheme
  debugging/how_to_dump_heap_profile_of_envoy

Performance
-----------

.. toctree::
  :maxdepth: 2

  performance/how_fast_is_envoy
  performance/how_to_benchmark_envoy

Configuration
-------------

.. toctree::
  :maxdepth: 2

  configuration/edge
  configuration/level_two
  configuration/sni
  configuration/zone_aware_routing
  configuration/tracing
  configuration/flow_control
  configuration/timeouts
  configuration/deprecation
  configuration/resource_limits

Load balancing
--------------

.. toctree::
  :maxdepth: 2

  load_balancing/lb_panic_threshold
  load_balancing/concurrency_lb
  load_balancing/disable_circuit_breaking
  load_balancing/transient_failures
  load_balancing/region_failover

Extensions
----------

.. toctree::
  :maxdepth: 2

  extensions/contract

Windows
-------

.. include:: ../_include/windows_support_ended.rst

.. toctree::
  :maxdepth: 2

  windows/win_requirements
  windows/win_not_supported_features
  windows/win_fips_support
  windows/win_performance
  windows/win_security
  windows/win_run_as_service
