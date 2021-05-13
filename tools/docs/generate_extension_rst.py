#!/usr/bin/env python3

# Generate RST lists of extensions grouped by their security posture.

from collections import defaultdict
import json
import os
import pathlib
import sys


def format_item(extension, metadata):
    if metadata['undocumented']:
        item = '* %s' % extension
    else:
        item = '* :ref:`%s <extension_%s>`' % (extension, extension)
    if metadata['status'] == 'alpha':
        item += ' (alpha)'
    return item


if __name__ == '__main__':
    extension_db_path = sys.argv[1]
    generated_rst_dir = sys.argv[2]
    security_rst_root = os.path.join(generated_rst_dir, "intro/arch_overview/security")
    extension_db = json.loads(pathlib.Path(extension_db_path).read_text())

    pathlib.Path(security_rst_root).mkdir(parents=True, exist_ok=True)

    security_postures = defaultdict(list)
    for extension, metadata in extension_db.items():
        security_postures[metadata['security_posture']].append(extension)

    for sp, extensions in security_postures.items():
        output_path = pathlib.Path(security_rst_root, 'secpos_%s.rst' % sp)
        content = '\n'.join(
            format_item(extension, extension_db[extension])
            for extension in sorted(extensions)
            if extension_db[extension]['status'] != 'wip')
        output_path.write_text(content)
