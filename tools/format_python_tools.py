import argparse
import fnmatch
import os
import sys

from yapf.yapflib.yapf_api import FormatFile


def collect_files():
  """Collect all Python files in the tools directory."""
  # TODO: Add ability to collect a specific file or files.
  matches = []
  for root, dirnames, filenames in os.walk('.'):
    for filename in fnmatch.filter(filenames, '*.py'):
      matches.append(os.path.join(root, filename))
  return matches


def validate_format(fix=False):
  """Check the format of python files in the tools directory.

    Arguments:
        fix: a flag to indicate if fixes should be applied.
    """
  fixes_required = False
  failed_update_files = set()
  successful_update_files = set()
  for file in collect_files():
    reformatted_source, encoding, changed = FormatFile(
        file, style_config='.style.yapf', in_place=fix, print_diff=not fix)
    if not fix:
      fixes_required = changed
      if reformatted_source:
        print(reformatted_source)
      continue
    file_list = failed_update_files if reformatted_source else successful_update_files  # noqa:E503
    file_list.add(file)
  if fix:
    display_fix_results(successful_update_files, failed_update_files)
    fixes_required = True if failed_update_files else False
  return not fixes_required


def display_fix_results(successful_files, failed_files):
  if successful_files:
    print('Successfully fixed {} files'.format(len(successful_files)))

  if failed_files:
    print('The following files failed to fix inline:')
    for file in failed_files:
      print('  - {}'.format(file))


if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Tool to format python files.')
  parser.add_argument(
      'action', choices=['check', 'fix'], default='check', help='Fix invalid syntax in files.')
  args = parser.parse_args()
  is_valid = validate_format(True if args.action == 'fix' else False)
  sys.exit(0 if is_valid else 1)
