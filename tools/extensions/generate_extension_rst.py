#!/usr/bin/env python3

# Generate RST lists of extensions grouped by their security posture.

from collections import defaultdict
import os
import pathlib

import yaml


def format_item(extension, metadata):
    if metadata.get('undocumented'):
        item = '* %s' % extension
    else:
        item = '* :ref:`%s <extension_%s>`' % (extension, extension)
    if metadata.get('status') == 'alpha':
        item += ' (alpha)'
    return item


if __name__ == '__main__':
    try:
        generated_rst_dir = os.environ["GENERATED_RST_DIR"]
    except KeyError:
        raise SystemExit(
            "Path to an output directory must be specified with GENERATED_RST_DIR env var")
    security_rst_root = os.path.join(generated_rst_dir, "intro/arch_overview/security")

    with open("source/extensions/extensions_metadata.yaml") as f:
        extension_db = yaml.safe_load(f.read())

    pathlib.Path(security_rst_root).mkdir(parents=True, exist_ok=True)

    security_postures = defaultdict(list)
    for extension, metadata in extension_db.items():
        security_postures[metadata['security_posture']].append(extension)

    for sp, extensions in security_postures.items():
        output_path = pathlib.Path(security_rst_root, 'secpos_%s.rst' % sp)
        content = '\n'.join(
            format_item(extension, extension_db[extension])
            for extension in sorted(extensions)
            if extension_db[extension].get('status') != 'wip')
        output_path.write_text(content)
