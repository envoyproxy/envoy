.. _start_quick_start:


Quick start
===========


Run the Envoy Docker image with the default configuration
---------------------------------------------------------

These instructions run from files in the Envoy repo. The sections below give a
more detailed explanation of the configuration file and execution steps for
the same configuration.

A very minimal Envoy configuration that can be used to validate basic plain HTTP
proxying is available in :repo:`configs/google_com_proxy.v2.yaml`. This is not
intended to represent a realistic Envoy deployment:

.. substitution-code-block:: console

  $ docker pull envoyproxy/|envoy_docker_image|
  $ docker run --rm -d -p 10000:10000 envoyproxy/|envoy_docker_image|
  $ curl -v localhost:10000

The Docker image used will contain the latest version of Envoy
and a basic Envoy configuration. This basic configuration tells
Envoy to route incoming requests to \*.google.com.


Override the default configuration by merging a config file
-----------------------------------------------------------

.. code-block:: yaml

   listeners:
     - name: listener_0
       address:
         socket_address:
           port_value: 20000

.. code-block:: console

  $ docker run --rm -d -v envoy-override.yaml:/envoy-override.yaml -p 20000:20000 envoyproxy/|envoy_docker_image| --config-yaml /envoy-override.yaml
  $ curl -v localhost:20000


Specify a custom configuration file
-----------------------------------

.. code-block:: console

  $ docker run --rm -d -v envoy-custom.yaml:/envoy-custom.yaml -p 10000:10000 envoyproxy/|envoy_docker_image| -c /envoy-custom.yaml


Configuration: static_resources
-------------------------------

.. literalinclude:: _include/example.yaml
    :language: yaml
    :linenos:
    :lines: 1-3
    :emphasize-lines: 1

Configuration: listeners
------------------------

.. literalinclude:: _include/example.yaml
    :language: yaml
    :linenos:
    :lines: 1-25
    :emphasize-lines: 3-23


Configuration: clusters
-----------------------

.. literalinclude:: _include/example.yaml
    :language: yaml
    :lineno-start: 22
    :lines: 22-47
    :emphasize-lines: 4-24

Configuration: admin
--------------------

.. literalinclude:: _include/example.yaml
    :language: yaml
    :lineno-start: 45
    :lines: 45-50
    :emphasize-lines: 3-6
