Config load check tool
======================

The config load check tool checks that a configuration file in json format is written using valid json
and conforms to the envoy json schema. This tool leverages the configuration test in
``test/config_test/config_test.cc``. The test loads the json configuration file and runs server configuration
initialization with it.

Input
  The tool expects a PATH to the root of a directory that holds json envoy configuration files. The tool
  will recursively go through the filesystem tree and run a configuration test for each configuration file found.

Output
  The tool will output envoy logs as it initializes the server configuration with the config it is currently testing.
  If there are configuration files where the json file is malformed or is does not conform to the envoy json schema, the
  tool will exit with status EXIT_FAILURE. If the tool successfully loads all configuration files found it will
  exit with status EXIT_SUCCESS.

Building
  The tool can be built locally using Bazel. ::

    bazel build //test/tools/config_load_check:config_load_check_tool

Running
  The tool takes a path as described above. ::

    bazel-bin/test/tools/config_load_check/config_load_check_tool PATH
