Added the :ref:`ORCA reporter filter <config_http_filters_orca_reporter>`, which adds the CPU and
memory utilization of configured cgroups, such as the Envoy container or the whole Kubernetes pod,
to responses as an ORCA load report for load aware load balancing on the client side.
