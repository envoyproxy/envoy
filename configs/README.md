# Envoy Configuration Validation

This directory contains example Envoy configuration files and a reusable Bazel macro for validating configuration files.

## Validating Configs in Envoy

The Envoy repository validates its own configuration files using:

```bash
bazel test //configs:envoy_configs_validation_test
```

This test validates all Envoy-owned config files in the `//configs:configs` filegroup.

## Using the Validation Macro in External Workspaces

External workspaces (e.g., envoy-docs) can use the reusable validation macro to validate their own config sets.

### Example Usage

In your external workspace's BUILD file:

```python
load("@envoy//configs:config_validation.bzl", "envoy_config_validation_test")

# Define your config filegroup
filegroup(
    name = "my_configs",
    srcs = glob(["**/*.yaml"]),
)

# Create a validation test
envoy_config_validation_test(
    name = "my_configs_validation_test",
    config_srcs = [":my_configs"],
    descriptor_set = "@envoy_api//:v3_proto_set",  # Use Envoy's API descriptors
)
```

Then run:

```bash
bazel test //:my_configs_validation_test
```

### Macro Parameters

- `name`: Name of the test target to create
- `config_srcs`: List of labels or filegroup containing config files to validate
- `descriptor_set`: Label of the protobuf descriptor set (default: `"@envoy_api//:v3_proto_set"`)
- `validation_script`: Label of the validation script (default: `"//configs:example_configs_validation.py"`)
- `deps`: Additional Python dependencies (default: `["envoy.base.utils"]`)
- `**kwargs`: Additional arguments passed to `py_test` (e.g., `tags`, `size`, `timeout`)

## Implementation Details

The validation macro:
- Wraps the validation script in `example_configs_validation.py`
- Creates a `py_test` target that validates YAML syntax and protobuf schema conformance
- Reports detailed validation errors
- Can be used independently by multiple workspaces without creating circular dependencies

This design supports the separation of the docs workspace from the core Envoy repository while maintaining consistent config validation across all Envoy-related projects.
