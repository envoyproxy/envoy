.. _start:

Getting Started
===============

This section gets you started with a very simple configuration and provides some example configurations.

The fastest way to get started using Envoy is :ref:`installing pre-built binaries <install_binaries>`.
You can also :ref:`build it <building>` from source.

These examples use the :ref:`v2 Envoy API <envoy_api_reference>`, but use only the static configuration
feature of the API, which is most useful for simple requirements. For more complex requirements
:ref:`Dynamic Configuration <arch_overview_dynamic_config>` is supported.

Quick Start to Run Simple Example
---------------------------------

These instructions run from files in the Envoy repo. The sections below give a
more detailed explanation of the configuration file and execution steps for
the same configuration.

A very minimal Envoy configuration that can be used to validate basic plain HTTP
proxying is available in :repo:`configs/google_com_proxy.v2.yaml`. This is not
intended to represent a realistic Envoy deployment:

.. substitution-code-block:: none

  $ docker pull envoyproxy/|envoy_docker_image|
  $ docker run --rm -d -p 10000:10000 envoyproxy/|envoy_docker_image|
  $ curl -v localhost:10000

The Docker image used will contain the latest version of Envoy
and a basic Envoy configuration. This basic configuration tells
Envoy to route incoming requests to \*.google.com.

Simple Configuration
--------------------

Envoy can be configured using a single YAML file passed in as an argument on the command line.

The :ref:`admin message <envoy_api_msg_config.bootstrap.v2.Admin>` is required to configure
the administration server. The `address` key specifies the
listening :ref:`address <envoy_api_file_envoy/api/v2/core/address.proto>`
which in this case is simply `0.0.0.0:9901`.

.. code-block:: yaml

  admin:
    access_log_path: /tmp/admin_access.log
    address:
      socket_address: { address: 0.0.0.0, port_value: 9901 }

The :ref:`static_resources <envoy_api_field_config.bootstrap.v2.Bootstrap.static_resources>` contains everything that is configured statically when Envoy starts,
as opposed to the means of configuring resources dynamically when Envoy is running.
The :ref:`v2 API Overview <config_overview_v2>` describes this.

.. code-block:: yaml

    static_resources:

The specification of the :ref:`listeners <envoy_api_file_envoy/api/v2/listener/listener.proto>`.

.. code-block:: yaml

      listeners:
      - name: listener_0
        address:
          socket_address: { address: 0.0.0.0, port_value: 10000 }
        filter_chains:
        - filters:
          - name: envoy.http_connection_manager
            typed_config:
              "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
              stat_prefix: ingress_http
              codec_type: AUTO
              route_config:
                name: local_route
                virtual_hosts:
                - name: local_service
                  domains: ["*"]
                  routes:
                  - match: { prefix: "/" }
                    route: { host_rewrite: www.google.com, cluster: service_google }
              http_filters:
              - name: envoy.router

The specification of the :ref:`clusters <envoy_api_file_envoy/api/v2/cds.proto>`.

.. code-block:: yaml

      clusters:
      - name: service_google
        connect_timeout: 0.25s
        type: LOGICAL_DNS
        # Comment out the following line to test on v6 networks
        dns_lookup_family: V4_ONLY
        lb_policy: ROUND_ROBIN
        load_assignment:
          cluster_name: service_google
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: www.google.com
                    port_value: 443
        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.api.v2.auth.UpstreamTlsContext
            sni: www.google.com


Using the Envoy Docker Image
----------------------------

Create a simple Dockerfile to execute Envoy, which assumes that envoy.yaml (described above) is in your local directory.
You can refer to the :ref:`Command line options <operations_cli>`.

.. substitution-code-block:: none

  FROM envoyproxy/|envoy_docker_image|
  COPY envoy.yaml /etc/envoy/envoy.yaml

Build the Docker image that runs your configuration using::

  $ docker build -t envoy:v1 .

And now you can execute it with::

  $ docker run -d --name envoy -p 9901:9901 -p 10000:10000 envoy:v1

And finally, test it using::

  $ curl -v localhost:10000

If you would like to use Envoy with docker-compose you can overwrite the provided configuration file
by using a volume.

.. substitution-code-block: yaml

  version: '3'
  services:
    envoy:
      image: envoyproxy/|envoy_docker_image|
      ports:
        - "10000:10000"
      volumes:
        - ./envoy.yaml:/etc/envoy/envoy.yaml


Sandboxes
---------

We've created a number of sandboxes using Docker Compose that set up different
environments to test out Envoy's features and show sample configurations. As we
gauge peoples' interests we will add more sandboxes demonstrating different
features. The following sandboxes are available:

.. toctree::
    :maxdepth: 2

    sandboxes/cors
    sandboxes/csrf
    sandboxes/fault_injection
    sandboxes/front_proxy
    sandboxes/grpc_bridge
    sandboxes/jaeger_native_tracing
    sandboxes/jaeger_tracing
    sandboxes/lua
    sandboxes/mysql
    sandboxes/redis
    sandboxes/zipkin_tracing

Other use cases
---------------

In addition to the proxy itself, Envoy is also bundled as part of several open
source distributions that target specific use cases.

.. toctree::
    :maxdepth: 2

    distro/ambassador
    distro/gloo
