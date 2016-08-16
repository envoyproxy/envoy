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
  configuration.

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
