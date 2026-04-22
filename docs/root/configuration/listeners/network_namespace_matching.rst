.. _config_listeners_network_namespace_matching:

Network Namespace Matching
==========================

Envoy supports routing connections to different filter chains based on the network namespace
of the listener address. This feature is particularly useful in containerized environments
where different services or environments are isolated using Linux network namespaces.

.. attention::

   Network namespace matching is only supported on Linux systems. On other platforms,
   the network namespace input will always return an empty value, causing connections
   to use the default filter chain.

Overview
--------

Network namespace matching allows you to:

* Route traffic from different network namespaces to different filter chains within a single listener.
* Implement multi-tenant architectures where each tenant has its own network namespace.
* Provide environment-specific routing like production, staging, or development based on namespace isolation.
* Maintain separate configurations for different containerized services.

The network namespace is determined by the ``network_namespace_filepath`` field in the
:ref:`SocketAddress <envoy_v3_api_msg_config.core.v3.SocketAddress>` configuration of the listener.

Configuration
-------------

Network namespace matching is configured using the :ref:`filter_chain_matcher
<envoy_v3_api_field_config.listener.v3.Listener.filter_chain_matcher>` field with the
``envoy.matching.inputs.network_namespace`` input.

Basic Configuration
~~~~~~~~~~~~~~~~~~~

.. code-block:: yaml

   listeners:
   - name: ns_aware_listener
     address:
       socket_address:
         protocol: TCP
         address: 127.0.0.1
         port_value: 8080
         network_namespace_filepath: "/var/run/netns/development"
     additional_addresses:
     - address:
         socket_address:
           protocol: TCP
           address: 127.0.0.1
           port_value: 8080
           network_namespace_filepath: "/var/run/netns/staging"
     - address:
         socket_address:
           protocol: TCP
           address: 127.0.0.1
           port_value: 8080
           network_namespace_filepath: "/var/run/netns/production"

     filter_chain_matcher:
       matcher_tree:
         input:
           name: envoy.matching.inputs.network_namespace
           typed_config:
             "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.NetworkNamespaceInput
         exact_match_map:
           map:
             "/var/run/netns/production":
               action:
                 name: production_chain
             "/var/run/netns/staging":
               action:
                 name: staging_chain
             "/var/run/netns/development":
               action:
                 name: development_chain

     filter_chains:
     - name: production_chain
       # Production-specific filters
     - name: staging_chain
       # Staging-specific filters
     - name: staging_chain
       # Development-specific filters

     default_filter_chain:
       # Default chain for unknown namespaces

Multiple Listeners Approach
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Alternatively, you can create separate listeners for each network namespace:

.. code-block:: yaml

   listeners:
   - name: production_listener
     address:
       socket_address:
         address: 127.0.0.1
         port_value: 8080
         network_namespace_filepath: "/var/run/netns/production"
     filter_chains:
     - # Production-specific configuration

   - name: staging_listener
     address:
       socket_address:
         address: 127.0.0.1
         port_value: 8080  # Same port, different namespace
         network_namespace_filepath: "/var/run/netns/staging"
     filter_chains:
     - # Staging-specific configuration

Input Behavior
--------------

The ``envoy.matching.inputs.network_namespace`` input:

* Returns the network namespace filepath as a string when available.
* Returns an empty value (no match) when:

  * No network namespace is configured for the listener address.
  * The network namespace filepath is empty.
  * Running on non-Linux platforms where network namespaces are not supported.
  * The address type doesn't support network namespaces (e.g., Unix domain sockets).

* Always returns the namespace of the listener's local address, not the client's namespace.

Matching Strategies
-------------------

Exact Match
~~~~~~~~~~~

Use exact string matching for specific namespaces:

.. code-block:: yaml

   exact_match_map:
     map:
       "/var/run/netns/production": { action: { name: "prod_chain" } }
       "/var/run/netns/staging": { action: { name: "staging_chain" } }

Prefix Match
~~~~~~~~~~~~

Use prefix matching for namespace hierarchies:

.. code-block:: yaml

   matcher_tree:
     input:
       name: envoy.matching.inputs.network_namespace
     prefix_match_map:
       map:
         "/var/run/netns/prod": { action: { name: "production_chain" } }
         "/var/run/netns/dev": { action: { name: "development_chain" } }

Complex Matching
~~~~~~~~~~~~~~~~

Combine with other inputs for sophisticated routing:

.. code-block:: yaml

   matcher_tree:
     input:
       name: envoy.matching.inputs.network_namespace
     exact_match_map:
       map:
         "/var/run/netns/production":
           matcher:
             matcher_tree:
               input:
                 name: envoy.matching.inputs.destination_port
               exact_match_map:
                 map:
                   "8080": { action: { name: "prod_http_chain" } }
                   "8443": { action: { name: "prod_https_chain" } }

Use Cases
---------

Multi-Tenant Architecture
~~~~~~~~~~~~~~~~~~~~~~~~~

Route requests from different tenants to isolated backend services:

.. code-block:: yaml

   filter_chain_matcher:
     matcher_tree:
       input:
         name: envoy.matching.inputs.network_namespace
       exact_match_map:
         map:
           "/var/run/netns/tenant_a":
             action: { name: "tenant_a_chain" }
           "/var/run/netns/tenant_b":
             action: { name: "tenant_b_chain" }

Environment Isolation
~~~~~~~~~~~~~~~~~~~~~

Separate production, staging, and development traffic:

.. code-block:: yaml

   filter_chain_matcher:
     matcher_tree:
       input:
         name: envoy.matching.inputs.network_namespace
       exact_match_map:
         map:
           "/var/run/netns/production":
             action: { name: "production_chain" }
           "/var/run/netns/staging":
             action: { name: "staging_chain" }
           "/var/run/netns/development":
             action: { name: "development_chain" }

Service Mesh Integration
~~~~~~~~~~~~~~~~~~~~~~~~

Route traffic based on service identity encoded in network namespaces:

.. code-block:: yaml

   filter_chain_matcher:
     matcher_tree:
       input:
         name: envoy.matching.inputs.network_namespace
       prefix_match_map:
         map:
           "/var/run/netns/service-":
             matcher:
               # Further routing based on service name extracted from namespace

Statistics
----------

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  "listener.<name>.filter_chain_selected.<chain_name>", Counter, "Total number of connections routed to the specified filter chain."
  "listener.<name>.no_filter_chain_match", Counter, "Total number of connections that did not match any filter chain."
  "listener_manager.listener_create_success", Counter, "Total number of successfully created listeners."
  "listener_manager.listener_create_failure", Counter, "Total number of listener creation failures."

Example Configuration
---------------------

See :repo:`network_namespace_matching_example.yaml <docs/root/configuration/listeners/network_namespace_matching_example.yaml>`
for a complete example configuration demonstrating various network namespace matching scenarios.
