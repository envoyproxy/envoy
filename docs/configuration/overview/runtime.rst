.. _config_runtime:

Runtime
=======

The :ref:`runtime configuration <arch_overview_runtime>` specifies the location of the local file
system tree that contains re-loadable configuration elements. If runtime is not configured, a "null"
provider is used which has the effect of using all defaults built into the code.

.. code-block:: json

  {
    "symlink_root": "...",
    "subdirectory": "...",
    "override_subdirectory": "..."
  }

symlink_root
  *(required, string)* The implementation assumes that the file system tree is accessed via a
  symbolic link. An atomic link swap is used when a new tree should be switched to. This
  parameter specifies the path to the symbolic link. Envoy will watch the location for changes
  and reload the file system tree when they happen.

subdirectory
  *(required, string)* Specifies the subdirectory to load within the root directory. This is useful
  if multiple systems share the same delivery mechanism. Envoy configuration elements can be
  contained in a dedicated subdirectory.

.. _config_runtime_override_subdirectory:

override_subdirectory
  *(optional, string)* Specifies an optional subdirectory to load within the root directory. If
  specified and the directory exists, configuration values within this directory will override those
  found in the primary subdirectory. This is useful when Envoy is deployed across many different
  types of servers. Sometimes it is useful to have a per service cluster directory for runtime
  configuration. See below for exactly how the override directory is used.

File system layout
------------------

Various sections of the configuration guide describe the runtime settings that are available.
For example, :ref:`here <config_cluster_manager_cluster_runtime>` are the runtime settings for
upstream clusters.

Assume the following configuration settings for runtime:

* *symlink_root*: ``/srv/runtime/current``
* *subdirectory*: ``envoy``
* *override_subdirectory*: ```envoy_override``

Each '.' in a runtime key indicates a new directory in the hierarchy, rooted at *symlink_root* +
*subdirectory*. For example, the *health_check.min_interval* path would have the following full
file system path:

``/srv/runtime/current/envoy/health_check/min_interval``

The terminal portion of the path is a file that generally contains a numeric value unless otherwise
documented. Envoy will strip white space as needed when reading the file.

The *override_subdirectory* is used along with the :option:`--service-cluster` CLI option. Assume
that :option:`--service-cluster` has been set to ``my-clustser``. Envoy will first look for the
*health_check.min_interval* key in the following full file system path:

``/srv/runtime/current/envoy_override/my-cluster/health_check/min_interval``

If found, the value will override any value found in the primary lookup path. This allows runtime to
be customized for individual clusters as needed on top of a global base.

Symbolic link management
------------------------

Envoy uses a fairly common pattern for atomic file system updates. Envoy assumes that an entire
runtime tree is written to disk, and the symbolic link pointed to by *symlink_root* is swapped
using the equivalent of the following command:

..  code-block:: console

  ln -s new current_tmp && mv -Tf current_tmp current

It's beyond the scope of this document how the file system data is deployed, garbage collected, etc.

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
  num_keys, Gauge, Number of keys currently loaded
