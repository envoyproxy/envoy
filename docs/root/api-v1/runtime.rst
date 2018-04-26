.. _config_runtime_v1:

Runtime
=======

Runtime :ref:`configuration overview <config_runtime>`.

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
