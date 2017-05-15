.. _install_ref_configs:

Reference configurations
========================

The source distribution includes a set of example configuration templates for each of the three
major Envoy deployment types:

* :ref:`Service to service <deployment_type_service_to_service>`
* :ref:`Front proxy <deployment_type_front_proxy>`
* :ref:`Double proxy <deployment_type_double_proxy>`

The goal of this set of example configurations is to demonstrate the full capabilities of Envoy in
a complex deployment. All features will not be applicable to all use cases. For full documentation
see the :ref:`configuration reference <config>`.

Configuration generator
-----------------------

Envoy configurations can become relatively complicated. At Lyft we use `jinja
<http://jinja.pocoo.org/>`_ templating to make the configurations easier to create and manage. The
source distribution includes a version of the configuration generator that loosely approximates what
we use at Lyft. We have also included three example configuration templates for each of the above
three scenarios.

* Generator script: :repo:`configs/configgen.py`
* Service to service template: :repo:`configs/envoy_service_to_service.template.json`
* Front proxy template: :repo:`configs/envoy_front_proxy.template.json`
* Double proxy template: :repo:`configs/envoy_double_proxy.template.json`

To generate the example configurations run the following from the root of the repo:

.. code-block:: console

  mkdir -p generated/configs
  bazel build //configs:example_configs
  tar xvf $PWD/bazel-genfiles/configs/example_configs.tar -C generated/configs

The previous command will produce three fully expanded configurations using some variables
defined inside of `configgen.py`. See the comments inside of `configgen.py` for detailed
information on how the different expansions work.

A few notes about the example configurations:

* An instance of :ref:`service discovery service <arch_overview_service_discovery_sds>` is assumed
  to be running at `discovery.yourcompany.net`.
* DNS for `yourcompany.net` is assumed to be setup for various things. Search the configuration
  templates for different instances of this.
* Tracing is configured for `LightStep <http://lightstep.com/>`_. To
  disable this or enable `Zipkin <http://zipkin.io>` tracing, delete or
  change the :ref:`tracing configuration <config_tracing>` accordingly.
* The configuration demonstrates the use of a :ref:`global rate limiting service
  <arch_overview_rate_limit>`. To disable this delete the :ref:`rate limit configuration
  <config_rate_limit_service>`.
* :ref:`Route discovery service <config_http_conn_man_rds>` is configured for the service to service
  reference configuration and it is assumed to be running at `rds.yourcompany.net`.
* :ref:`Cluster discovery service <config_cluster_manager_cds>` is configured for the service to
  service reference configuration and it is assumed that be running at `cds.yourcompany.net`.

Smoketest configuration
-----------------------

A very minimal Envoy configuration that can be used to validate basic plain HTTP proxying is
available in :repo:`configs/google_com_proxy.json`. This is not intended to represent a realistic
Envoy deployment. To smoketest Envoy with this, run:

.. code-block:: console

  build/source/exe/envoy -c configs/google_com_proxy.json -l debug
  curl -v localhost:10000
