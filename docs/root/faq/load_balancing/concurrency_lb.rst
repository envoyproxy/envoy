Why doesn't RR load balancing appear to be even?
================================================

Envoy utilizes a siloed :ref:`threading model <arch_overview_threading>`. This means that worker
threads and the load balancers that run on them do not coordinate with each other. When utilizing
load balancing policies such as :ref:`round robin <arch_overview_load_balancing_types_round_robin>`,
it may thus appear that load balancing is not working properly when using multiple workers. The
:option:`--concurrency` option can be used to adjust the number of workers if desired.

The siloed execution model is also the reason why multiple HTTP/2 connections may be established to
each upstream; :ref:`connection pools <arch_overview_conn_pool>` are not shared between workers.
