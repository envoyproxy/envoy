.. _config_http_filters_composite:

Composite Filter
================

The composite filter allows delegating filter actions to a filter specified by a
:ref:`match result <arch_overview_matching_api>`. The purpose of this is to allow different filters
or filter configurations to be selected based on the incoming request, allowing for more dynamic
configuration that could become prohibitive when making use of per route configurations (e.g.
because the cardinality would cause a route table explosion).

The filter does not do any kind of buffering, and as a result it must be able to instantiate the
filter it will delegate to before it receives any callbacks that it needs to delegate. Because of
this, in order to delegate all the data to the specified filter, the decision must be made based
on just the request headers.

Delegation can fail if the filter factory attempted to use a callback not supported by the
composite filter. In either case, the ``<stat_prefix>.composite.delegation_error`` stat will be
incremented.

This filter adds a map of the delegated filter name (of the action that is matched )and the root config filter name to the filter state with key
``envoy.extensions.filters.http.composite.matched_actions``
This filter state is not emitted when the filter is configured in the upstream filter chain.

Contains a map of pairs `FILTER_CONFIG_NAME:ACTION_NAME`:

  * ``FILTER_CONFIG_NAME``: root filter config name;
  * ``ACTION_NAME``: delegated filter name of the action that is matched.


Sample Envoy configuration
--------------------------

Here's a sample Envoy configuration that makes use of the composite filter to inject a different
latency via the :ref:`fault filter <config_http_filters_fault_injection>`. It uses the header
``x-fault-category`` to determine which fault configuration to use: if the header is equal to the
string ``huge fault``, a 10s latency is injected while if the header contains ``tiny fault`` a 1s
latency is injected. If the header is absent or contains a different value, no filter is
instantiated.

.. literalinclude:: _include/composite.yaml
    :language: yaml

Statistics
----------

The composite filter outputs statistics in the ``<stat_prefix>.composite.*`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  delegation_success, Counter, Number of requests that successfully created a delegated filter
  delegation_error, Counter, Number of requests that attempted to create a delegated filter but failed
