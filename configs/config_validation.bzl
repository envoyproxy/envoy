"""
Bazel macro for Envoy configuration validation.

This module provides a reusable macro for validating Envoy configuration files
against protobuf descriptors. It can be used by Envoy itself or by external
workspaces/modules (e.g., envoy-docs) to validate their own config sets.

Example usage in an external workspace (e.g., envoy-docs):

    # In your WORKSPACE or MODULE.bazel file:
    # Add envoy as a dependency to access the validation macro
    
    # In your BUILD file:
    load("@envoy//configs:config_validation.bzl", "envoy_config_validation_test")
    
    envoy_config_validation_test(
        name = "docs_configs_validation_test",
        config_srcs = [":my_docs_configs"],  # Your filegroup of config files
        descriptor_set = "@envoy_api//:v3_proto_set",  # Or your own descriptor
    )

The macro wraps the validation script and creates a py_test target that:
- Validates YAML syntax
- Validates configs against Envoy protobuf schemas
- Reports validation errors with details

This allows each workspace to independently validate its own config sets without
creating circular dependencies.
"""

load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_test")

def envoy_config_validation_test(
        name,
        config_srcs,
        descriptor_set = "@envoy_api//:v3_proto_set",
        validation_script = "//configs:example_configs_validation.py",
        deps = None,
        **kwargs):
    """Creates a test target to validate Envoy configuration files.
    
    This macro generates a py_test target that runs the Envoy config validation
    script against the specified configuration files. The validation ensures
    that config files are valid YAML and conform to the Envoy protobuf schemas.
    
    Args:
        name: Name of the test target to create.
        config_srcs: List of labels or a filegroup containing config files to validate.
            Can be a mix of individual .yaml files and filegroups.
        descriptor_set: Label of the protobuf descriptor set to use for validation.
            Defaults to "@envoy_api//:v3_proto_set".
        validation_script: Label of the Python validation script to use.
            Defaults to "//configs:example_configs_validation.py".
        deps: Additional Python dependencies. If None, defaults to ["envoy.base.utils"].
        **kwargs: Additional arguments to pass to py_test (e.g., tags, size, timeout).
    """
    if not deps:
        deps = [requirement("envoy.base.utils")]
    
    # Ensure config_srcs is a list
    if type(config_srcs) != type([]):
        config_srcs = [config_srcs]
    
    py_test(
        name = name,
        srcs = [validation_script],
        main = validation_script,
        args = [
            "--descriptor_path=$(location %s)" % descriptor_set,
        ] + ["$(locations %s)" % src for src in config_srcs],
        data = config_srcs + [descriptor_set],
        deps = deps,
        **kwargs
    )
