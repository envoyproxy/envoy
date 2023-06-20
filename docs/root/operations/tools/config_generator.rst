.. _start_tools_configuration_generator:

Configuration generator
-----------------------

Envoy configurations can become relatively complicated. The
source distribution includes a version of the configuration generator that uses `jinja
<http://jinja.pocoo.org/>`_ templating to make the configurations easier to create and manage. We
have also included three example configuration templates for each of the above three scenarios.

* Generator script: :repo:`configs/configgen.py`
* Service to service template: :repo:`configs/envoy_service_to_service.template.yaml`
* Front proxy template: :repo:`configs/envoy_front_proxy.template.yaml`
* Double proxy template: :repo:`configs/envoy_double_proxy.template.yaml`

To generate the example configurations run the following from the root of the repo:

.. code-block:: console

  mkdir -p generated/configs
  bazel build //configs:example_configs
  tar xvf $PWD/bazel-out/k8-fastbuild/bin/configs/example_configs.tar -C generated/configs

The previous command will produce three fully expanded configurations using some variables
defined inside of ``configgen.py``. See the comments inside of ``configgen.py`` for detailed
information on how the different expansions work.

A few notes about the example configurations:

* An instance of :ref:`endpoint discovery service <arch_overview_service_discovery_types_eds>` is assumed
  to be running at ``discovery.yourcompany.net``.
* DNS for ``yourcompany.net`` is assumed to be setup for various things. Search the configuration
  templates for different instances of this.
* Tracing is configured for `Jaeger <https://jaegertracing.io/>`_. To
  disable this or enable `Zipkin <https://zipkin.io>`_ or `Datadog <https://datadoghq.com>`_ tracing, delete or
  change the :ref:`tracing configuration <envoy_v3_api_file_envoy/config/trace/v3/trace.proto>` accordingly.
* The configuration demonstrates the use of a :ref:`global rate limiting service
  <arch_overview_global_rate_limit>`. To disable this delete the :ref:`rate limit configuration
  <config_rate_limit_service>`.
* :ref:`Route discovery service <config_http_conn_man_rds>` is configured for the service to service
  reference configuration and it is assumed to be running at ``rds.yourcompany.net``.
* :ref:`Cluster discovery service <config_cluster_manager_cds>` is configured for the service to
  service reference configuration and it is assumed that be running at ``cds.yourcompany.net``.
