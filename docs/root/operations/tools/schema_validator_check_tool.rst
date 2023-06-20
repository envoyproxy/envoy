.. _install_tools_schema_validator_check_tool:

Schema validator check tool
===========================

The schema validator tool validates that the passed in configuration conforms to
a given schema. The configuration may be JSON or YAML. To validate the entire
config, please refer to the
:ref:`config load check tool<install_tools_config_load_check_tool>`.

Input
  The tool expects two required inputs:

  1. The schema type to check the passed in configuration against. The supported types are:

    * ``route`` - for :ref:`route configuration<envoy_v3_api_msg_config.route.v3.RouteConfiguration>` validation.
    * ``discovery_response`` for :ref:`discovery response<envoy_v3_api_msg_service.discovery.v3.DiscoveryResponse>` validation.
    * ``bootstrap`` for :ref:`bootstrap<envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` validation.

  2. The path to the configuration file.

  Optional inputs include:

  1. ``--fail-on-deprecated``: Will force failure if any deprecated fields are used.
  2. ``--fail-on-wip``: Will force failure if any work-in-progress fields are used.

Output
  If the configuration conforms to the schema, the tool will exit with status
  EXIT_SUCCESS. If the configuration does not conform to the schema, an error
  message is outputted detailing what doesn't conform to the schema. The tool
  will exit with status EXIT_FAILURE.

Building
  The tool is included in the :ref:`tools image <install_tools>`.

  The tool can also be built locally using Bazel. ::

    bazel build //test/tools/schema_validator:schema_validator_tool

Running
  The tool takes a path as described above. ::

    bazel-bin/test/tools/schema_validator/schema_validator_tool  --schema-type SCHEMA_TYPE  --config-path PATH
