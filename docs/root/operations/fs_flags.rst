.. _operations_file_system_flags:

File system flags
=================

Envoy supports file system "flags" that alter state at startup. This is used to persist changes
between restarts if necessary. The flag files should be placed in the directory specified in the
:ref:`flags_path <config_overview_flags_path>` configuration option. The currently supported
flag files are:

drain
  If this file exists, Envoy will start in HC failing mode, similar to after the
  :http:post:`/healthcheck/fail` command has been executed.
