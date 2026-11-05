.. _config_bootstrap_extensions_dynamic_modules:

Dynamic Modules
===============

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension>`

The Dynamic Modules bootstrap extension allows you to write bootstrap extensions in a dynamic module.
This can be used to implement:

* Server initialization logic when Envoy starts.
* Code that runs on every worker thread.
* Clusters that the module adds, updates and removes.
* Singleton patterns for configuration loading from external services.
* Global state management across filters.
* Background tasks that run on the main thread.

The extension is configured using the :ref:`DynamicModuleBootstrapExtension <envoy_v3_api_msg_extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension>` message.

Example Configuration
---------------------

.. code-block:: yaml

  bootstrap_extensions:
  - name: envoy.bootstrap.dynamic_modules
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension
      dynamic_module_config:
        name: my_module
      extension_name: my_bootstrap_extension
      extension_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: "my_config"

Lifecycle Hooks
---------------

Bootstrap extensions have two lifecycle hooks:

**onServerInitialized**
  Called on the main thread once Envoy created its cluster manager and listener manager, and before
  it starts its workers. Clusters can still be initializing. This is where you can:

  * Load configuration from external services.
  * Initialize global state.
  * Register singleton resources.
  * Start background tasks.

**onWorkerThreadInitialized**
  Called once on the main thread after Envoy creates its workers and before it starts their
  threads. To run code on every worker thread once the server is initialized, see :ref:`Running Code
  on Workers <config_bootstrap_extensions_dynamic_modules_run_on_all_workers>`.

.. _config_bootstrap_extensions_dynamic_modules_run_on_all_workers:

Running Code on Workers
-----------------------

A module calls ``envoy_dynamic_module_callback_bootstrap_extension_run_on_all_workers`` on the
main thread to run its ``envoy_dynamic_module_on_bootstrap_extension_worker_event`` hook on every
worker thread. Once every worker ran the event,
``envoy_dynamic_module_on_bootstrap_extension_run_on_all_workers_done`` runs on the main thread. A
worker runs the event after it applied the cluster and host changes that Envoy posted to it
before the call, so the done hook tells the module that every worker can use them. Host health,
weight and metadata changes that a cluster merges over its :ref:`update_merge_window
<envoy_v3_api_field_config.cluster.v3.Cluster.CommonLbConfig.update_merge_window>` can reach the
workers later.

The workers start only after Envoy finishes initializing, so a module must not wait for the done
hook before it calls ``envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete``.
That call can start the workers and wait until they ran the events posted before it, so a module
must not hold a lock across it that its worker event hook takes.

.. _config_bootstrap_extensions_dynamic_modules_cluster_writer:

Cluster Writer
--------------

Once the module calls ``envoy_dynamic_module_callback_bootstrap_extension_enable_cluster_writer``,
it can add, update and remove clusters. It passes a serialized
:ref:`Cluster <envoy_v3_api_msg_config.cluster.v3.Cluster>` to
``envoy_dynamic_module_callback_bootstrap_extension_cluster_writer_add_or_update``, and Envoy
reports the outcome of each write through
``envoy_dynamic_module_on_bootstrap_extension_cluster_writer_update_done``. A write is active once
the cluster finished warming. CDS does not remove the clusters that the module writes, and the
module should only write clusters that no other component writes. Envoy does not wait for written
clusters before it finishes initializing, so to serve them from the first request, call
``envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete`` only after their
writes completed as active.

A written cluster has the same authority as a cluster delivered by CDS, so build clusters from a
schema that the module owns. On the ADS stream and the xDS-TP authority streams, a written cluster
holds back the CDS requests while it warms, as a cluster delivered by CDS does, unless its
:ref:`wait_for_warm_on_init <envoy_v3_api_field_config.cluster.v3.Cluster.wait_for_warm_on_init>`
is false. While Envoy applies a burst of writes, it holds back the EDS, LEDS and SDS requests on
those streams, including their ACKs, for about one second, as it does while it applies a CDS
update. A write that changes a cluster
rebuilds it and drains its connection pools, so change endpoints through a dynamic module cluster
instead.

The config dump lists written clusters as dynamic clusters with the ``version_info`` of their last
change. Envoy does not time out a write that never finishes warming. Such a write holds back the
CDS requests until the module writes the cluster again or removes it, and a module that holds back
``envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete`` until its writes
are active should bound that wait itself.

The cluster writer emits the following statistics with the ``dynamic_modules.cluster_writer.``
prefix, tagged with ``config_name`` set to the extension name, or ``default`` if it is empty.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  update_submitted, Counter, Writes that Envoy accepted
  update_rejected, Counter, Writes that Envoy refused before the server was initialized or after shutdown began
  update_active, Counter, Writes that became active
  update_superseded, Counter, Writes that a later write replaced before they became active
  update_removed, Counter, Writes that a removal ended before they became active
  update_failed, Counter, Writes that failed when Envoy applied them
  update_ownership_lost, Counter, Writes that ended because another component removed the cluster
  ownership_lost, Counter, Clusters that another component replaced or removed
  clusters_owned, Gauge, Clusters that the module owns
  updates_pending, Gauge, Writes that are queued or warming

Use Cases
---------

**Dynamic Configuration Loading**
  Use the ``onServerInitialized`` hook to fetch configuration from an external service (e.g., a config server)
  at startup. Store the configuration in shared state that filters can access.

**Singleton Pattern**
  Implement a singleton that is initialized once and shared across all filters. For example, a connection pool
  to an external service, or a cache that is shared across all requests.

**Global Metrics**
  Initialize custom metrics in the ``onServerInitialized`` hook that can be updated by filters throughout
  the request lifecycle.

For more details on dynamic modules, see the :ref:`architecture overview <arch_overview_dynamic_modules>`.
