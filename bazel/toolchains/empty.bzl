_BAZEL_TO_CONFIG_SPEC_NAMES = {}

# sha256 digest of the latest version of the toolchain container.
LATEST = ""

_CONTAINER_TO_CONFIG_SPEC_NAMES = {}

_DEFAULT_TOOLCHAIN_CONFIG_SPEC = ""

_TOOLCHAIN_CONFIG_SPECS = []

TOOLCHAIN_CONFIG_AUTOGEN_SPEC = struct(
    bazel_to_config_spec_names_map = _BAZEL_TO_CONFIG_SPEC_NAMES,
    container_to_config_spec_names_map = _CONTAINER_TO_CONFIG_SPEC_NAMES,
    default_toolchain_config_spec = _DEFAULT_TOOLCHAIN_CONFIG_SPEC,
    latest_container = LATEST,
    toolchain_config_specs = _TOOLCHAIN_CONFIG_SPECS,
)
