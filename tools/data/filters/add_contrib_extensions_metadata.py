from envoy_repo.contrib import extensions_metadata


def main(data):
    contrib_extensions = extensions_metadata.data.copy()
    for v in contrib_extensions.values():
        v['contrib'] = True
    data.update(contrib_extensions)
    return data
