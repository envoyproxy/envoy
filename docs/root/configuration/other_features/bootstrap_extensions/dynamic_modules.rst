.. _config_bootstrap_extensions_dynamic_modules:

Dynamic Modules
===============

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension>`

The Dynamic Modules bootstrap extension allows you to write bootstrap extensions in a dynamic module.
This can be used to implement:

* Server initialization logic when Envoy starts.
* Code that runs on every worker thread.
* Clusters, listeners and filter chains that the module adds, updates and removes.
* Reactions to runtime value changes.
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
worker runs the event after it applied the cluster, host, listener and filter chain changes that
Envoy posted to it before the call, so the done hook tells the module that the workers can use them.
Host health, weight and metadata changes that a cluster merges over its :ref:`update_merge_window
<envoy_v3_api_field_config.cluster.v3.Cluster.CommonLbConfig.update_merge_window>` can reach the
workers later.

The workers start only after Envoy finishes initializing, so a module must not wait for the done
hook before it calls ``envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete``.
That call can start the workers and wait until they ran the events posted before it, so a module
must not hold a lock across it that its worker event hook takes.

.. _config_bootstrap_extensions_dynamic_modules_runtime_update:

Runtime Updates
---------------

A bootstrap extension watches a :ref:`runtime <config_runtime>` key by passing it to
``envoy_dynamic_module_callback_bootstrap_extension_enable_runtime_update``, once for each key to
watch. Envoy then calls a runtime update hook on that extension on the main thread whenever the
value of a watched key changes. The hook is chosen by the type of the value, the way the runtime
getters read it:

* ``envoy_dynamic_module_on_bootstrap_extension_runtime_int_update`` for a non-negative integer,
  as read by ``envoy_dynamic_module_callback_get_runtime_int``.
* ``envoy_dynamic_module_on_bootstrap_extension_runtime_number_update`` for any other number, as
  read by ``envoy_dynamic_module_callback_get_runtime_number``.
* ``envoy_dynamic_module_on_bootstrap_extension_runtime_bool_update`` for a boolean, as read by
  ``envoy_dynamic_module_callback_get_runtime_bool``.

A value of any other type, such as a string, counts as unset. When a key becomes unset, Envoy calls
the hook of its previous type with the previous value and ``removed`` set. Envoy compares each
newly loaded runtime snapshot with the previous values of the watched keys, so an RTDS update, a
disk layer update or a request to the admin ``/runtime_modify`` endpoint reports only the watched
keys whose type or value it changed, in lexicographic order. The value of a key when the extension
starts watching it is not reported. When a hook runs, the runtime getters already return the new
value on the main thread. When a runtime guard such as ``envoy.reloadable_features.some_flag`` is
removed, the guard returns to its value before any layer set it, normally its compiled default,
while ``envoy_dynamic_module_callback_get_runtime_bool`` returns the default given by the caller.

The extension can only watch keys during
``envoy_dynamic_module_on_bootstrap_extension_server_initialized``. RTDS may or may not have
delivered its layers by then, so the extension should read the current value with the runtime
getters when it starts watching a key, and then rely on the hooks for the later changes. No update
is reported once the server began to shut down. In the Rust SDK, the three hooks call
``on_runtime_update`` of ``BootstrapExtension`` with a ``RuntimeValue``, or with ``None`` when the
key became unset.

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
update. A burst of the listener writer extends this hold. A write that changes a cluster
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

.. _config_bootstrap_extensions_dynamic_modules_listener_writer:

Listener Writer
---------------

Once the module calls ``envoy_dynamic_module_callback_bootstrap_extension_enable_listener_writer``,
it can add, update and remove listeners. It passes a serialized
:ref:`Listener <envoy_v3_api_msg_config.listener.v3.Listener>` to
``envoy_dynamic_module_callback_bootstrap_extension_listener_writer_add_or_update``, and Envoy
reports the outcome of each write through
``envoy_dynamic_module_on_bootstrap_extension_listener_writer_update_done``. A write is active once
the listener finished warming and Envoy posted it to the workers. A State-of-the-World LDS update
does not remove the listeners that the module writes, and the module should only write listeners
that no other component writes.

Writes that the module submits before it calls
``envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete`` are applied before
Envoy finishes initializing, and Envoy starts its workers once their listeners warmed, so they serve
from the first connection. Those writes complete as active once the workers started, so a module
must not wait for them before it signals that its initialization is complete.

A written listener has the same authority as a listener delivered by LDS, so build listeners from a
schema that the module owns. While Envoy applies a burst of writes, it holds back the RDS, SRDS,
VHDS, FCDS and SDS requests on the ADS stream and the xDS-TP authority streams, including their
ACKs, for about one second, as it does while it applies an LDS update. A burst of the cluster writer
extends this hold. When a written listener fails to listen after it warmed, its write fails. If the
write added addresses in place, the workers keep serving the previous version. Otherwise Envoy
removes the listener and its previous version, and the module no longer owns it. The error details
of the write say which of the two happened.

The config dump lists written listeners as dynamic listeners with the ``version_info`` of their last
change, except that a change of only the filter chains of an active listener can keep the previous
version. When Envoy fails to create the listener of a write, the config dump shows the error until a
later write of the listener succeeds, the module removes the listener, or an LDS update arrives.
Envoy does not time out a write that never finishes warming, so a module that waits for its writes
to become active should bound that wait itself.

The listener writer emits the following statistics with the ``dynamic_modules.listener_writer.``
prefix, tagged with ``config_name`` set to the extension name, or ``default`` if it is empty.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  update_submitted, Counter, Writes that Envoy accepted
  update_rejected, Counter, Writes that Envoy refused before the server was initialized or after shutdown began
  update_active, Counter, Writes that became active
  update_superseded, Counter, Writes that a later write replaced before they became active
  update_removed, Counter, Writes that a removal ended before they became active
  update_failed, Counter, Writes that failed when Envoy applied them or whose listener failed to listen
  update_ownership_lost, Counter, Writes that ended because another component replaced or removed the listener
  ownership_lost, Counter, Listeners that another component replaced or removed
  listeners_owned, Gauge, Listeners that the module owns
  updates_pending, Gauge, Writes that are queued or warming or that wait for the workers to start

.. _config_bootstrap_extensions_dynamic_modules_filter_chain_config_writer:

Filter Chain Config Writer
--------------------------

Once the module calls
``envoy_dynamic_module_callback_bootstrap_extension_enable_filter_chain_config_writer``, it can add,
update and remove the filter chains that the listeners with ``fcds_config`` find by name, as the
Filter Chain Discovery Service (FCDS) would deliver them. It passes a serialized :ref:`FilterChain
<envoy_v3_api_msg_config.listener.v3.FilterChain>` to
``envoy_dynamic_module_callback_bootstrap_extension_filter_chain_config_writer_add_or_update``, and
Envoy reports the outcome of each write through
``envoy_dynamic_module_on_bootstrap_extension_filter_chain_config_writer_update_done``. A write is
active once the filter chain finished warming and Envoy posted it to the workers. A listener whose
``fcds_config`` has no ``config_source`` waits, without a timeout, until the filter chains that it
finds by name are written and finished warming, so the module can write a listener before its filter
chains. Such a listener that Envoy creates before it starts its workers holds back the
initialization of Envoy until then. A name is either discovered through FCDS or written by an
extension. Envoy rejects a listener that requests a name of the other kind, and a write of a name
that a listener requests through FCDS completes as ``Failed``. A listener keeps its names until it
is removed and has finished draining. Envoy does not notice when another extension writes or removes
the same filter chain, so the module should only write filter chains that no other extension writes.

A written filter chain has the same authority as a filter chain delivered by FCDS, so build filter
chains from a schema that the module owns. A write that changes a filter chain drains the
connections of its previous version on the listeners that use it, and a removal drains all its
connections and ends the wait of the listeners that wait for it. The config dump does not list
written filter chains. Envoy does not time out a write that never finishes warming, or a listener
that waits for a filter chain that is never written, so a module that waits for either should bound
that wait itself.

The filter chain config writer emits the following statistics with the
``dynamic_modules.filter_chain_config_writer.`` prefix, tagged with ``config_name`` set to the
extension name, or ``default`` if it is empty.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  update_submitted, Counter, Writes that Envoy accepted
  update_rejected, Counter, Writes that Envoy refused before the server was initialized or after shutdown began
  update_active, Counter, Writes that became active
  update_superseded, Counter, Writes that a later write replaced before they became active
  update_removed, Counter, Writes that a removal ended before they became active
  update_failed, Counter, Writes that failed when Envoy applied them
  filter_chains_owned, Gauge, Filter chains that the module owns
  updates_pending, Gauge, Writes that are queued or warming

.. _config_bootstrap_extensions_dynamic_modules_ecds:

ECDS Configuration
------------------

A module calls ``envoy_dynamic_module_callback_bootstrap_extension_subscribe_ecds`` after server
initialization to subscribe to one named
:ref:`TypedExtensionConfig <envoy_v3_api_msg_config.core.v3.TypedExtensionConfig>` resource. It
passes a JSON-encoded :ref:`ExtensionConfigSource
<envoy_v3_api_msg_config.core.v3.ExtensionConfigSource>` with its configuration source and allowed
``type_urls``. Default configurations and wildcard names are not supported. Another bootstrap
extension cannot use the same ADS or xDS-TP name. Filter ECDS subscriptions conflict only on ADS.
The first subscriber keeps the name until its subscription ends. The bootstrap subscription ends
when shutdown begins.

``envoy_dynamic_module_on_bootstrap_extension_ecds_update`` receives the resource name, version,
payload type, encoding and configuration on the main thread. ``Struct`` and ``TypedStruct.value``
arrive as JSON, ``StringValue`` and ``BytesValue`` as unwrapped bytes, and other messages as
protobuf bytes. xDS-TP names have sorted context parameters. Filesystem updates without
``version_info`` have an empty version.

For types that Envoy does not link, use ``TypedStruct``, or send binary payloads as ``BytesValue``;
an accepted payload that Envoy cannot render fails the whole unfiltered config dump. Identical
resends and empty state-of-the-world updates do not invoke the hook.

The hook must not block. Rejecting an update keeps the previous configuration and NACKs the
response, which may contain other ECDS resources and may have been partly applied. Explicit
removals and TTL expiry invoke the hook without a configuration. Delta can report a missing
resource as a removal before any configuration arrives. A rejected removal is only logged and
keeps the previous configuration. A connection failure keeps the last configuration unless its
TTL expires.

Filesystem sources can invoke the hook before the subscription call returns, so do not hold a
lock across the call that the hook takes. Initial fetch failures do not invoke the hook. A module
that waits for its first configuration before signaling initialization complete must set its own
startup timeout.

The ``ecds_modules`` config dump key lists accepted bootstrap configurations with their version
and update time. Subscription statistics use the
``extension_config_discovery.bootstrap.<resource_name>.<extension_name>.`` prefix, tagged with
``config_name`` set to the extension name, or ``default`` if it is empty, when explicit stats tags
are enabled.

A refused removal still increments ``update_success`` and updates the subscription version.
On TTL expiry, ``version_text`` becomes empty. The module and config dump keep their previous
configuration.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  init_fetch_timeout, Counter, Initial configuration fetches that timed out
  update_attempt, Counter, Configuration fetch attempts
  update_failure, Counter, Configuration fetch failures
  update_rejected, Counter, Rejected configuration updates
  update_success, Counter, Successful subscription updates including unchanged resends and refused removals
  update_time, Gauge, Time of the last successful subscription update in milliseconds since the epoch
  update_duration, Histogram, Time spent processing updates in milliseconds
  version, Gauge, Hash of the last successful subscription version
  version_text, TextReadout, Last successful subscription version

.. _config_bootstrap_extensions_dynamic_modules_config_snapshots:

Configuration Snapshots
-----------------------

Bootstrap modules can collect selected configuration providers with
``envoy_dynamic_module_callback_bootstrap_extension_config_snapshot_new``. The Rust SDK exposes
this as ``get_config_snapshot``.

Provider keys include ``bootstrap``, ``clusters``, ``listeners``,
``routes``, ``route_scopes``, ``secrets`` and the registered ``ecds_filter_*`` providers. Missing keys
are omitted, duplicates are collected once, and an empty selection collects nothing. Providers
appear in key order, as in :ref:`/config_dump <operations_admin_interface_config_dump>`.
Endpoint dumps are not included. An optional full-match RE2 regular expression filters resource names
for providers that support it. A missing or empty expression disables filtering.

Collection and sensitive-field redaction run synchronously on the main thread after server
initialization and before shutdown begins. Their cost grows with the selected resources. Combine
frequent notifications into one collection.

Collection is refused in cluster or listener lifecycle hooks, whatever triggered them, and in
other bootstrap hooks Envoy runs inside an xDS update or a resource's destruction. Providers can
contain partially applied changes with an old version. Defer collection with the bootstrap config
scheduler. Callout failures during cluster removal are already reported from a posted callback.

Creation returns a result distinguishing a refused call, an invalid or oversized name expression,
unavailable admin support, and a provider failure. Calls are refused off the main thread, before
initialization, after shutdown begins, inside those hooks, or while another collection runs.

JSON encoding can also be expensive. Use a module-owned thread to avoid blocking Envoy's main thread
or workers. Encoding and deletion can run on any thread. Release each snapshot before its owning
bootstrap configuration's destroy hook returns. Modules must stop and join workers holding snapshots
during configuration teardown. A snapshot must not be used concurrently.

Redaction covers only protobuf fields annotated as sensitive, as with ``/config_dump``. Metadata,
opaque strings and module configuration can still contain sensitive data. JSON encoding can fail
when an ``Any`` contains an unknown or unlinked protobuf type.

This API needs admin support compiled in, but does not need an admin listener and is not restricted
by admin ``allow_paths``.

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
