"""Transform the extensions metadata dict into the security postures dict"""

from collections import defaultdict

from envoy.docs import abstract


def main(data: abstract.ExtensionsMetadataDict) -> abstract.ExtensionSecurityPosturesDict:
    security_postures = defaultdict(list)
    for extension, metadata in data.items():
        security_postures[metadata['security_posture']].append(extension)
    return security_postures
