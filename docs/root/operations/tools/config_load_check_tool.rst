.. _install_tools_config_load_check_tool:

Config load check tool
======================

The config load check tool checks that a configuration file in JSON format is written using valid JSON
and conforms to the Envoy JSON schema. This tool leverages the configuration test in
``test/config_test/config_test.cc``. The test loads the JSON configuration file and runs server configuration
initialization with it.

Input
  The tool expects a PATH to the root of a directory that holds JSON Envoy configuration files. The tool
  will recursively go through the file system tree and run a configuration test for each file found. Keep in mind that
  the tool will try to load all files found in the path.

Output
  The tool will output Envoy logs as it initializes the server configuration with the config it is currently testing.
  If there are configuration files where the JSON file is malformed or is does not conform to the Envoy JSON schema, the
  tool will exit with status EXIT_FAILURE. If the tool successfully loads all configuration files found it will
  exit with status EXIT_SUCCESS.

Building
  The tool can be built locally using Bazel. ::

    bazel build //test/tools/config_load_check:config_load_check_tool

Running
  The tool takes a path as described above. ::

    bazel-bin/test/tools/config_load_check/config_load_check_tool PATH
