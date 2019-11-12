# Generate pseudo API docs for extensions that have google.protobuf.Empty
# config.

import json
import pathlib
import string
import sys

import protodoc

EMPTY_EXTENSION_DOCS_TEMPLATE = string.Template("""$header

$description

$reflink

This extension does not have a structured configuration, `google.protobuf.Empty
<https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#empty>`_ should be used
instead.

$extension
""")


def GenerateEmptyExtensionsDocs(extension, details, api_extensions_root):
  extension_root = pathlib.Path(details['path'])
  path = pathlib.Path(api_extensions_root, extension_root, 'empty', extension_root.name + '.rst')
  path.parent.mkdir(parents=True, exist_ok=True)
  description = details.get('description', '')
  reflink = ''
  if 'ref' in details:
    reflink = '%s %s.' % (details['title'],
                          protodoc.FormatInternalLink('configuration overview', details['ref']))
  content = EMPTY_EXTENSION_DOCS_TEMPLATE.substitute(header=protodoc.FormatHeader(
      '=', details['title']),
                                                     description=description,
                                                     reflink=reflink,
                                                     extension=protodoc.FormatExtension(extension))
  path.write_text(content)


if __name__ == '__main__':
  empty_extensions_path = sys.argv[1]
  api_extensions_root = sys.argv[2]

  empty_extensions = json.loads(pathlib.Path(empty_extensions_path).read_text())
  for extension, details in empty_extensions.items():
    GenerateEmptyExtensionsDocs(extension, details, api_extensions_root)
