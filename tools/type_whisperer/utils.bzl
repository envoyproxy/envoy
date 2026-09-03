"Shared utilities for type_whisperer Starlark rules."

def normalize_workspace_name(ws_name):
    """Strips the canonical Bazel repo-name suffix from ws_name.

    Under bzlmod, Bazel appends a '+' (Bazel 7+) or '~' (older bzlmod)
    separator followed by a version string to create the canonical name.
    This function returns the base module name, e.g.
      "envoy_api+" -> "envoy_api"
      "envoy_api~1.0" -> "envoy_api"
      "envoy_api" -> "envoy_api"
      "" -> ""
    """
    if not ws_name:
        return ""
    return ws_name.split("+", 1)[0].split("~", 1)[0]
