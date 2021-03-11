#!/usr/bin/env python3

# Generate RST lists of external dependencies.

from collections import defaultdict, namedtuple
import pathlib
import sys
import urllib.parse

from tools.dependency import utils as dep_utils


# Render a CSV table given a list of table headers, widths and list of rows
# (each a list of strings).
def CsvTable(headers, widths, rows):
  csv_rows = '\n  '.join(', '.join(row) for row in rows)
  return f'''.. csv-table::
  :header: {', '.join(headers)}
  :widths: {', '.join(str(w) for w in widths) }

  {csv_rows}

'''


# Anonymous external RST link for a given URL.
def RstLink(text, url):
  return f'`{text} <{url}>`__'


# NIST CPE database search URL for a given CPE.
def NistCpeUrl(cpe):
  encoded_cpe = urllib.parse.quote(cpe)
  return f'https://nvd.nist.gov/vuln/search/results?form_type=Advanced&results_type=overview&query={encoded_cpe}&search_type=all'


# Render version strings human readable.
def RenderVersion(version):
  # Heuristic, almost certainly a git SHA
  if len(version) == 40:
    # Abbreviate git SHA
    return version[:7]
  return version


def RenderTitle(title):
  underline = '~' * len(title)
  return f'\n{title}\n{underline}\n\n'


# Determine the version link URL. If it's GitHub, use some heuristics to figure
# out a release tag link, otherwise point to the GitHub tree at the respective
# SHA. Otherwise, return the tarball download.
def GetVersionUrl(metadata):
  # Figure out if it's a GitHub repo.
  github_release = dep_utils.GetGitHubReleaseFromUrls(metadata['urls'])
  # If not, direct download link for tarball
  if not github_release:
    return metadata['urls'][0]
  github_repo = f'https://github.com/{github_release.organization}/{github_release.project}'
  if github_release.tagged:
    # The GitHub version should look like the metadata version, but might have
    # something like a "v" prefix.
    return f'{github_repo}/releases/tag/{github_release.version}'
  assert (metadata['version'] == github_release.version)
  return f'{github_repo}/tree/{github_release.version}'


if __name__ == '__main__':
  security_rst_root = sys.argv[1]

  Dep = namedtuple('Dep', ['name', 'sort_name', 'version', 'cpe', 'release_date'])
  use_categories = defaultdict(lambda: defaultdict(list))
  # Bin rendered dependencies into per-use category lists.
  for k, v in dep_utils.RepositoryLocations().items():
    cpe = v.get('cpe', '')
    if cpe == 'N/A':
      cpe = ''
    if cpe:
      cpe = RstLink(cpe, NistCpeUrl(cpe))
    project_name = v['project_name']
    project_url = v['project_url']
    name = RstLink(project_name, project_url)
    version = RstLink(RenderVersion(v['version']), GetVersionUrl(v))
    release_date = v['release_date']
    dep = Dep(name, project_name.lower(), version, cpe, release_date)
    for category in v['use_category']:
      for ext in v.get('extensions', ['core']):
        use_categories[category][ext].append(dep)

  def CsvRow(dep):
    return [dep.name, dep.version, dep.release_date, dep.cpe]

  # Generate per-use category RST with CSV tables.
  for category, exts in use_categories.items():
    content = ''
    for ext_name, deps in sorted(exts.items()):
      if ext_name != 'core':
        content += RenderTitle(ext_name)
      output_path = pathlib.Path(security_rst_root, f'external_dep_{category}.rst')
      content += CsvTable(['Name', 'Version', 'Release date', 'CPE'], [2, 1, 1, 2],
                          [CsvRow(dep) for dep in sorted(deps, key=lambda d: d.sort_name)])
    output_path.write_text(content)
