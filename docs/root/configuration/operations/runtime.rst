.. _config_runtime:

Runtime
=======

The :ref:`runtime configuration <arch_overview_runtime>` specifies a virtual file system tree that
contains re-loadable configuration elements. This virtual file system can be realized via a series
of local file system, static bootstrap configuration, RTDS and admin console derived overlays.

* :ref:`v3 API reference <envoy_v3_api_msg_config.bootstrap.v3.Runtime>`

.. _config_virtual_filesystem:

Virtual file system
-------------------

.. _config_runtime_layering:

Layering
++++++++

The runtime can be viewed as a virtual file system consisting of multiple layers. The :ref:`layered
runtime <envoy_v3_api_msg_config.bootstrap.v3.LayeredRuntime>` bootstrap configuration specifies this
layering. Runtime settings in later layers override earlier layers. A typical configuration might
be:

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.LayeredRuntime

  layers:
  - name: static_layer_0
    static_layer:
      health_check:
        min_interval: 5
  - name: disk_layer_0
    disk_layer: { symlink_root: /srv/runtime/current, subdirectory: envoy }
  - name: disk_layer_1
    disk_layer: { symlink_root: /srv/runtime/current, subdirectory: envoy_override, append_service_cluster: true }
  - name: admin_layer_0
    admin_layer: {}

In the deprecated :ref:`runtime <envoy_v3_api_msg_config.bootstrap.v3.Runtime>` bootstrap
configuration, the layering was implicit and fixed:

1. :ref:`Static bootstrap configuration <config_runtime_bootstrap>`
2. :ref:`Local disk file system <config_runtime_local_disk>`
3. :ref:`Local disk file system *override_subdirectory* <config_runtime_local_disk>`
4. :ref:`Admin console overrides <config_runtime_admin>`

with values in higher layers overriding corresponding values in lower layers.

.. _config_runtime_file_system:

File system layout
++++++++++++++++++

Various sections of the configuration guide describe the runtime settings that are available.
For example, :ref:`here <config_cluster_manager_cluster_runtime>` are the runtime settings for
upstream clusters.

Each '.' in a runtime key indicates a new directory in the hierarchy,
The terminal portion of a path is the file. The contents of the file constitute the runtime value.
When reading numeric values from a file, spaces and new lines will be ignored.

*numerator* or *denominator* are reserved keywords and may not appear in any directory.

.. _config_runtime_bootstrap:

Static bootstrap
++++++++++++++++

A static base runtime may be specified in the :ref:`bootstrap configuration
<envoy_v3_api_field_config.bootstrap.v3.Runtime.base>` via a :ref:`protobuf JSON representation
<config_runtime_proto_json>`.

.. _config_runtime_local_disk:

Local disk file system
++++++++++++++++++++++

When the :ref:`runtime virtual file system <config_runtime_file_system>` is realized on a local
disk, it is rooted at *symlink_root* +
*subdirectory*. For example, the *health_check.min_interval* key would have the following full
file system path (using the symbolic link):

``/srv/runtime/current/envoy/health_check/min_interval``

.. _config_runtime_local_disk_overrides:

Overrides
~~~~~~~~~

An arbitrary number of disk file system layers can be overlaid in the :ref:`layered
runtime <envoy_v3_api_msg_config.bootstrap.v3.LayeredRuntime>` bootstrap configuration.

In the deprecated :ref:`runtime <envoy_v3_api_msg_config.bootstrap.v3.Runtime>` bootstrap configuration,
there was a distinguished file system override. Assume that the folder ``/srv/runtime/v1`` points to
the actual file system path where global runtime configurations are stored. The following would be a
typical configuration setting for runtime:

* *symlink_root*: ``/srv/runtime/current``
* *subdirectory*: ``envoy``
* *override_subdirectory*: ``envoy_override``

Where ``/srv/runtime/current`` is a symbolic link to ``/srv/runtime/v1``.

.. _config_runtime_local_disk_service_cluster_subdirs:

Cluster-specific subdirectories
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In the deprecated :ref:`runtime <envoy_v3_api_msg_config.bootstrap.v3.Runtime>` bootstrap configuration,
the *override_subdirectory* is used along with the :option:`--service-cluster` CLI option. Assume
that :option:`--service-cluster` has been set to ``my-cluster``. Envoy will first look for the
*health_check.min_interval* key in the following full file system path:

``/srv/runtime/current/envoy_override/my-cluster/health_check/min_interval``

If found, the value will override any value found in the primary lookup path. This allows the user
to customize the runtime values for individual clusters on top of global defaults.

With the :ref:`layered runtime <envoy_v3_api_msg_config.bootstrap.v3.LayeredRuntime>` bootstrap
configuration, it is possible to specialize on service cluster via the :ref:`append_service_cluster
<envoy_v3_api_field_config.bootstrap.v3.RuntimeLayer.DiskLayer.append_service_cluster>` option at any
disk layer.

.. _config_runtime_symbolic_link_swap:

Updating runtime values via symbolic link swap
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are two steps to update any runtime value. First, create a hard copy of the entire runtime
tree and update the desired runtime values. Second, atomically swap the symbolic link root from the
old tree to the new runtime tree, using the equivalent of the following command:

.. code-block:: console

  /srv/runtime:~$ ln -s /srv/runtime/v2 new && mv -Tf new current

It's beyond the scope of this document how the file system data is deployed, garbage collected, etc.

.. _config_runtime_rtds:

Runtime Discovery Service (RTDS)
++++++++++++++++++++++++++++++++

One or more runtime layers may be specified and delivered by specifying a :ref:`rtds_layer
<envoy_v3_api_field_config.bootstrap.v3.RuntimeLayer.rtds_layer>`. This points the runtime layer at a
regular :ref:`xDS <xds_protocol>` endpoint, subscribing to a single xDS resource for the given
layer. The resource type for these layers is a :ref:`Runtime message
<envoy_v3_api_msg_service.runtime.v3.Runtime>`.

.. _config_runtime_admin:

Admin console
+++++++++++++

Values can be viewed at the
:ref:`/runtime admin endpoint <operations_admin_interface_runtime>`. Values can be modified and
added at the :ref:`/runtime_modify admin endpoint <operations_admin_interface_runtime_modify>`. If
runtime is not configured, an empty provider is used which has the effect of using all defaults
built into the code, except for any values added via `/runtime_modify`.

.. attention::

  Use the :ref:`/runtime_modify<operations_admin_interface_runtime_modify>` endpoint with care.
  Changes are effectively immediately. It is **critical** that the admin interface is :ref:`properly
  secured <operations_admin_interface_security>`.

At most one admin layer may be specified. If a non-empty :ref:`layered runtime
<envoy_v3_api_msg_config.bootstrap.v3.LayeredRuntime>` bootstrap configuration is specified with an
absent admin layer, any mutating admin console actions will elicit a 503 response.

.. _config_runtime_atomicity:

Atomicity
---------

The runtime will reload and a new snapshot will be generated in a variety of situations, i.e.:

* When a file move operation is detected under the symlink root or the symlink root changes.
* When an admin console override is added or modified.

All runtime layers are evaluated during a snapshot. Layers with errors are ignored and excluded from
the effective layers, see :ref:`num_layers <runtime_stats>`. Walking the symlink root will take a
non-zero amount of time, so if true atomicity is desired, the runtime directory should be immutable
and symlink changes should be used to orchestrate updates.

Disk layers with the same symlink root will only trigger a single refresh when a file movement is
detected. Disk layers with overlapping symlink root paths that are not identical may trigger
multiple reloads when a file movement is detected.

.. _config_runtime_proto_json:

Protobuf and JSON representation
--------------------------------

The runtime :ref:`file system <config_runtime_file_system>` can be represented inside a proto3
message as a `google.protobuf.Struct
<https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#google.protobuf.Struct>`_
modeling a JSON object with the following rules:

* Dot separators map to tree edges.
* Scalar leaves (integer, strings, booleans, doubles) are represented with their respective JSON type.
* :ref:`FractionalPercent <envoy_v3_api_msg_type.v3.FractionalPercent>` is represented with via its
  `canonical JSON encoding <https://developers.google.com/protocol-buffers/docs/proto3#json>`_.

An example representation of a setting for the *health_check.min_interval* key in YAML is:

.. code-block:: yaml

  health_check:
    min_interval: 5

.. note::

  Integer values that are parsed from doubles are rounded down to the nearest whole number.

.. _config_runtime_comments:

Comments
--------

Lines starting with ``#`` as the first character are treated as comments.

Comments can be used to provide context on an existing value. Comments are also useful in an
otherwise empty file to keep a placeholder for deployment in a time of need.

.. _config_runtime_deprecation:

Using runtime overrides for deprecated features
-----------------------------------------------

The Envoy runtime is also a part of the Envoy feature deprecation process.

As described in the Envoy :repo:`breaking change policy <CONTRIBUTING.md#breaking-change-policy>`,
feature deprecation in Envoy is in 3 phases: warn-by-default, fail-by-default, and code removal.

In the first phase, Envoy logs a warning to the warning log that the feature is deprecated and
increments the :ref:`deprecated_feature_use <runtime_stats>` runtime stat.
Users are encouraged to go to :ref:`deprecated <deprecated>` to see how to
migrate to the new code path and make sure it is suitable for their use case.

In the second phase the field will be tagged as disallowed_by_default
and use of that configuration field will cause the config to be rejected by default.
This disallowed mode can be overridden in runtime configuration by setting
envoy.deprecated_features:full_fieldname or envoy.deprecated_features:full_enum_value
to true. For example, for a deprecated field
``Foo.Bar.Eep`` set ``envoy.deprecated_features:Foo.bar.Eep`` to
``true``. There is a production example using static runtime to allow both fail-by-default fields here:
:repo:`configs/using_deprecated_config.v2.yaml`
Use of these override is **strongly discouraged** so please use with caution and switch to the new fields
as soon as possible. Fatal-by-default configuration indicates that the removal of the old code paths is
imminent. It is far better for both Envoy users and for Envoy contributors if any bugs or feature gaps
with the new code paths are flushed out ahead of time, rather than after the code is removed!

.. _runtime_stats:

.. attention::

   Versions of Envoy prior to 1.14.1 cannot parse runtime booleans from integer values and require
   an explicit "true" or "false". Mistakenly placing an integer such as "0" to represent "false"
   will lead to usage of the default value. This is especially important to keep in mind for case of
   runtime overrides for :ref:`deprecated features<deprecated>`, as it will can potentially result
   in unexpected Envoy behaviors.

Statistics
----------

The file system runtime provider emits some statistics in the *runtime.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  admin_overrides_active, Gauge, 1 if any admin overrides are active otherwise 0
  deprecated_feature_use, Counter, Total number of times deprecated features were used. Detailed information about the feature used will be logged to warning logs in the form "Using deprecated option 'X' from file Y".
  deprecated_feature_seen_since_process_start, Gauge, Number of times deprecated features were used. This is not carried over during hot restarts.
  load_error, Counter, Total number of load attempts that resulted in an error in any layer
  load_success, Counter, Total number of load attempts that were successful at all layers
  num_keys, Gauge, Number of keys currently loaded
  num_layers, Gauge, Number of layers currently active (without loading errors)
  override_dir_exists, Counter, Total number of loads that did use an override directory
  override_dir_not_exists, Counter, Total number of loads that did not use an override directory
