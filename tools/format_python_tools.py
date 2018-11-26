import argparse
import fnmatch
import os
import sys

from yapf.yapflib.yapf_api import FormatFile

EXCLUDE_LIST = ['pyformat']


def collectFiles():
  """Collect all Python files in the tools directory.

  Returns: A collection of python files in the tools directory excluding
    any directories in the EXCLUDE_LIST constant.
  """
  # TODO: Add ability to collect a specific file or files.
  matches = []
  for root, dirnames, filenames in os.walk('.'):
    dirnames[:] = [d for d in dirnames if d not in EXCLUDE_LIST]
    for filename in fnmatch.filter(filenames, '*.py'):
      matches.append(os.path.join(root, filename))
  return matches


def validateFormat(fix=False):
  """Check the format of python files in the tools directory.

    Arguments:
      fix: a flag to indicate if fixes should be applied.
    """
  fixes_required = False
  failed_update_files = set()
  successful_update_files = set()
  for python_file in collectFiles():
    reformatted_source, encoding, changed = FormatFile(
        python_file, style_config='.style.yapf', in_place=fix, print_diff=not fix)
    if not fix:
      fixes_required = True if changed else fixes_required
      if reformatted_source:
        print(reformatted_source)
      continue
    file_list = failed_update_files if reformatted_source else successful_update_files
    file_list.add(python_file)
  if fix:
    displayFixResults(successful_update_files, failed_update_files)
    fixes_required = len(failed_update_files) > 0
  return not fixes_required


def displayFixResults(successful_files, failed_files):
  if successful_files:
    print('Successfully fixed {} files'.format(len(successful_files)))

  if failed_files:
    print('The following files failed to fix inline:')
    for failed_file in failed_files:
      print('  - {}'.format(failed_file))


if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Tool to format python files.')
  parser.add_argument(
      'action', choices=['check', 'fix'], default='check', help='Fix invalid syntax in files.')
  args = parser.parse_args()
  is_valid = validateFormat(args.action == 'fix')
  sys.exit(0 if is_valid else 1)
