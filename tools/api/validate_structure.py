#!/usr/bin/env python3

# Validate the API package structure. Usage:
#
# ./tools/api/validate_structure.py

import pathlib
import re
import sys

# Only v2 protos are allowed in these trees.
V2_ONLY_PATHS = [
    'api',
    'config/filter',
    'config/transport_socket',
    'config/common/dynamic_forward_proxy',
    'config/common/tap',
]

# These are trees that allow v3+ protos, but only a strict whitelist.
V3_RESTRICTED_PATHS = {
    'config/accesslog/v3': ['accesslog.proto'],
    'service/discovery/v3': ['ads.proto', 'discovery.proto'],
}

# These are the only legacy trees that we permit not to terminate with a versioned suffix.
VERSIONLESS_PATHS = [
    'annotations',
    'api/v2/ratelimit',
    'api/v2/auth',
    'api/v2/listener',
    'api/v2/core',
    'api/v2/endpoint',
    'api/v2/route',
    'api/v2/cluster',
    'type',
    'type/matcher',
    'config/cluster/redis',
    'config/retry/previous_priorities',
]


class ValidationError(Exception):
  pass


# Extract major version and full API version string from a proto path.
def ProtoApiVersion(proto_path):
  match = re.match('v(\d+).*', proto_path.parent.name)
  if match:
    return str(proto_path.parent.name)[1:], int(match.group(1))
  return None, 0


# Validate a single proto path.
def ValidateProtoPath(proto_path):
  version_str, major_version = ProtoApiVersion(proto_path)

  # Validate version-less paths.
  if major_version == 0:
    if not any(str(proto_path.parent) == p for p in VERSIONLESS_PATHS):
      raise ValidationError('Package is missing a version')

  # Validate that v3+ versions are regular.
  if major_version >= 3:
    if not re.match('\d+(alpha)?$', version_str):
      raise ValidationError('Invalid v3+ version: %s' % version_str)

    # Validate v2-only paths.
    for p in V2_ONLY_PATHS:
      if str(proto_path).startswith(p):
        raise ValidationError('v3+ protos are not allowed in %s' % p)

    # Validate v3 restricted paths.
    for p in V3_RESTRICTED_PATHS:
      if str(proto_path).startswith(p):
        allowed_files = V3_RESTRICTED_PATHS[p]
        if proto_path.name not in allowed_files:
          raise ValidationError('Only %s allowed in %s' % (allowed_files, p))


# Validate a list of proto paths.
def ValidateProtoPaths(proto_paths):
  error_msgs = []
  for proto_path in proto_paths:
    try:
      ValidateProtoPath(proto_path)
    except ValidationError as e:
      error_msgs.append('Invalid .proto location [%s]: %s' % (proto_path, e))
  return error_msgs


if __name__ == '__main__':
  api_root = 'api/envoy'
  api_protos = pathlib.Path(api_root).rglob('*.proto')
  error_msgs = ValidateProtoPaths(p.relative_to(api_root) for p in api_protos)
  if error_msgs:
    for m in error_msgs:
      print(m)
    sys.exit(1)
  sys.exit(0)
