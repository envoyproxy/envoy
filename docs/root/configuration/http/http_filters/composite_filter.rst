.. _config_http_filters_composite:

Composite Filter
================

The composite filter allows delegating filter actions to a filter specified by a 
:ref:`match result <arch_overview_matching_api>`. The purpose of this is to allow different filters
or filter configurations to be selected based on the incoming request/response, allowing for more
dynamic configuration that could become prohibitive when making use of per route configurations
(e.g. because the cardinality would cause a route table explosion).


Sample Envoy configuration
--------------------------

Here's a sample Envoy configuration that makes use of the composite filter to inject a different
latency via the :ref:`fault filter <config_http_filters_fault_injection>`. It uses the header
`x-fault-category` to determine which fault configuration to use: if the header is equal to the
string `huge fault`, a 10s latency is injected while if the header contains `tiny string` a 10ms
latency is injected. If the header is absent or contains a different value, no filter is
instantiated.

.. literalinclude:: _include/composite.yaml
    :language: yaml