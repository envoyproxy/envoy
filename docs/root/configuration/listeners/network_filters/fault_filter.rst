.. _config_network_filters_fault_injection:

Network Fault Injection
=======================

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.fault.v3.NetworkFault``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.fault.v3.NetworkFault>`

Overview
--------

The network fault injection filter can be used to test the resiliency of TCP services and
proxies by aborting a configured percentage of incoming connections before passing them to
subsequent network filters (such as :ref:`tcp_proxy <config_network_filters_tcp_proxy>`).

When an incoming connection matches an enforced abort fault:

* The connection is closed immediately (``ConnectionCloseType::NoFlush``) before downstream
  network filters run, preventing upstream connection attempts.
* The ``FI`` (``FaultInjected``) response flag and ``fault_filter_abort`` response code /
  connection termination details are recorded on the connection's stream info.
* If :ref:`filter_metadata <envoy_v3_api_field_extensions.filters.network.fault.v3.NetworkFault.filter_metadata>`
  is configured, it is written to the connection's dynamic metadata under the
  ``envoy.filters.network.fault`` namespace and can be logged via
  ``%DYNAMIC_METADATA(envoy.filters.network.fault)%``.

Dry-Run Mode
------------

When :ref:`dry_run <envoy_v3_api_field_extensions.filters.network.fault.v3.FaultAbort.dry_run>`
is set to ``true``, the filter evaluates the abort percentage without closing the connection or
setting the ``FI`` response flag. Instead, it increments the ``aborts_dry_run`` stat and merges
``{"dry_run_abort": true}`` into the ``envoy.filters.network.fault`` dynamic metadata namespace
(preserving any other keys already present in that namespace). User-configured
:ref:`filter_metadata <envoy_v3_api_field_extensions.filters.network.fault.v3.NetworkFault.filter_metadata>`
is **not** written for dry-run matches, so access logs and dashboards can distinguish dry-run
matches (via ``%DYNAMIC_METADATA(envoy.filters.network.fault:dry_run_abort)%``) from enforced
fault aborts.

Example
-------

.. code-block:: yaml

  filter_chains:
  - filters:
    - name: envoy.filters.network.fault
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.fault.v3.NetworkFault
        stat_prefix: my_tcp_fault
        abort:
          percentage:
            numerator: 5
            denominator: HUNDRED
          dry_run: true
        # Written only when an abort is enforced (dry_run: false).
        filter_metadata:
          experiment_name: "my_experiment"
    - name: envoy.filters.network.tcp_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
        stat_prefix: destination
        cluster: some_service

.. _config_network_filters_fault_injection_runtime:

Runtime
-------

The network fault injection filter supports overriding the abort percentage via runtime:

fault.network.abort.abort_percent
  The percentage of incoming connections to abort (or record in ``dry_run`` mode). Defaults to the
  :ref:`percentage <envoy_v3_api_field_extensions.filters.network.fault.v3.FaultAbort.percentage>`
  specified in the filter config. This runtime key can be customized per filter via
  :ref:`abort_percent_runtime <envoy_v3_api_field_extensions.filters.network.fault.v3.NetworkFault.abort_percent_runtime>`.
  This key is only evaluated when :ref:`abort <envoy_v3_api_field_extensions.filters.network.fault.v3.NetworkFault.abort>`
  is configured.

.. _config_network_filters_fault_injection_stats:

Statistics
----------

The filter emits statistics under ``fault.<stat_prefix>.``, where
:ref:`stat_prefix <envoy_v3_api_field_extensions.filters.network.fault.v3.NetworkFault.stat_prefix>`
is the configured prefix:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  aborts_injected, Counter, Total connections aborted by the network fault filter.
  aborts_dry_run, Counter, Total connections matching an abort fault in ``dry_run`` mode without being closed.
