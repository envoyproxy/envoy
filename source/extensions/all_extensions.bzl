# Return all extensions to be compiled into Envoy.
# TODO(mattklein123): Every extension should have an independent Bazel select option that will
# allow us to compile in and out different extensions. We may also consider in the future other
# selection options such as maturity.
def envoy_all_extensions(repository = ""):
  return [
    repository + "//source/extensions/filters/network/echo:config",
  ]

