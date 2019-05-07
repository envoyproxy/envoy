.. _config_runtime:

Runtime
=======

The :ref:`runtime configuration <arch_overview_runtime>` specifies a virtual file system tree that
contains re-loadable configuration elements. This virtual file system can be realized via a series
of local file system, static bootstrap configuration and admin console derived overlays.

* :ref:`v2 API reference <envoy_api_msg_config.bootstrap.v2.Runtime>`

.. _config_virtual_filesystem:

Virtual file system
-------------------

.. _config_runtime_layering:

Layering
++++++++

The runtime can be viewed as virtual file system consisting of multiple layers:

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
<envoy_api_field_config.bootstrap.v2.Runtime.base>` via a :ref:`protobuf JSON representation
<config_runtime_proto_json>`.

.. _config_runtime_local_disk:

Local disk file system
++++++++++++++++++++++

When the :ref:`runtime virtual file system <config_runtime_file_system>` is realized on a local
disk, it is rooted at *symlink_root* +
*subdirectory*. For example, the *health_check.min_interval* key would have the following full
file system path (using the symbolic link):

``/srv/runtime/current/envoy/health_check/min_interval``

Assume that the folder ``/srv/runtime/v1`` points to the actual file system path where global
runtime configurations are stored. The following would be a typical configuration setting for
runtime:

* *symlink_root*: ``/srv/runtime/current``
* *subdirectory*: ``envoy``
* *override_subdirectory*: ``envoy_override``

Where ``/srv/runtime/current`` is a symbolic link to ``/srv/runtime/v1``.

The *override_subdirectory* is used along with the :option:`--service-cluster` CLI option. Assume
that :option:`--service-cluster` has been set to ``my-cluster``. Envoy will first look for the
*health_check.min_interval* key in the following full file system path:

``/srv/runtime/current/envoy_override/my-cluster/health_check/min_interval``

If found, the value will override any value found in the primary lookup path. This allows the user
to customize the runtime values for individual clusters on top of global defaults.

.. _config_runtime_symbolic_link_swap:

Updating runtime values via symbolic link swap
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are two steps to update any runtime value. First, create a hard copy of the entire runtime
tree and update the desired runtime values. Second, atomically swap the symbolic link root from the
old tree to the new runtime tree, using the equivalent of the following command:

.. code-block:: console

  /srv/runtime:~$ ln -s /srv/runtime/v2 new && mv -Tf new current

It's beyond the scope of this document how the file system data is deployed, garbage collected, etc.

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

.. _config_runtime_proto_json:

Protobuf and JSON representation
--------------------------------

The runtime :ref:`file system <config_runtime_file_system>` can be represented inside a proto3
message as a `google.protobuf.Struct
<https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#google.protobuf.Struct>`_
modeling a JSON object with the following rules:

* Dot separators map to tree edges.
* Scalar leaves (integer, strings, booleans) are represented with their respective JSON type.
* :ref:`FractionalPercent <envoy_api_msg_type.FractionalPercent>` is represented with via its
  `canonical JSON encoding <https://developers.google.com/protocol-buffers/docs/proto3#json>`_.

An example representation of a setting for the *health_check.min_interval* key in YAML is:

.. code-block:: yaml

  health_check:
    min_interval: 5

.. _config_runtime_comments:

Comments
--------

Lines starting with ``#`` as the first character are treated as comments.

Comments can be used to provide context on an existing value. Comments are also useful in an
otherwise empty file to keep a placeholder for deployment in a time of need.

Using runtime overrides for deprecated features
-----------------------------------------------

The Envoy runtime is also a part of the Envoy feature deprecation process.

As described in the Envoy :repo:`breaking change policy <CONTRIBUTING.md#breaking-change-policy>`,
feature deprecation in Envoy is in 3 phases: warn-by-default, fail-by-default, and code removal.

In the first phase, Envoy logs a warning to the warning log that the feature is deprecated and
increments the :ref:`deprecated_feature_use <runtime_stats>` runtime stat.
Users are encouraged to go to :ref:`deprecated <deprecated>` to see how to
migrate to the new code path and make sure it is suitable for their use case.

In the second phase the message and filename will be added to
:repo:`runtime_features.cc <source/common/runtime/runtime_features.cc>`
and use of that configuration field will cause the config to be rejected by default. 
This fail-by-default mode can be overridden in runtime configuration by setting
envoy.deprecated_features.filename.proto:fieldname to true. For example, for a deprecated field
``Foo.Bar.Eep`` in ``baz.proto`` set ``envoy.deprecated_features.baz.proto:Eep`` to
``true``. Use of this override is **strongly discouraged**.
Fatal-by-default configuration indicates that the removal of the old code paths is imminent. It is
far better for both Envoy users and for Envoy contributors if any bugs or feature gaps with the new
code paths are flushed out ahead of time, rather than after the code is removed!

.. _runtime_stats:

Statistics
----------

The file system runtime provider emits some statistics in the *runtime.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  load_error, Counter, Total number of load attempts that resulted in an error
  override_dir_not_exists, Counter, Total number of loads that did not use an override directory
  override_dir_exists, Counter, Total number of loads that did use an override directory
  load_success, Counter, Total number of load attempts that were successful
  deprecated_feature_use, Counter, Total number of times deprecated features were used.
  num_keys, Gauge, Number of keys currently loaded
