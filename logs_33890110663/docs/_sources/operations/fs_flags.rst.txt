.. _operations_file_system_flags:

File system flags
=================

Envoy supports file system "flags" that alter state at startup. This is used to
persist changes between restarts if necessary. The flag files should be placed
in the directory specified in the :ref:`flags_path
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.flags_path>` configuration
option. The currently supported flag files are:

drain
  If this file exists, Envoy will start in health check failing mode, similar to after the
  :http:post:`/healthcheck/fail` command has been executed.
