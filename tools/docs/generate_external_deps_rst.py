#!/usr/bin/env python3

# Generate RST lists of external dependencies.

import json
import os
import pathlib
import sys
import tarfile
import urllib.parse
from collections import defaultdict, namedtuple

# Information releated to a GitHub release version.
GitHubRelease = namedtuple('GitHubRelease', ['organization', 'project', 'version', 'tagged'])


# Search through a list of URLs and determine if any contain a GitHub URL. If
# so, use heuristics to extract the release version and repo details, return
# this, otherwise return None.
def get_github_release_from_urls(urls):
    for url in urls:
        if not url.startswith('https://github.com/'):
            continue
        components = url.split('/')
        if components[5] == 'archive':
            # Only support .tar.gz, .zip today. Figure out the release tag from this
            # filename.
            if components[-1].endswith('.tar.gz'):
                github_version = components[-1][:-len('.tar.gz')]
            else:
                assert (components[-1].endswith('.zip'))
                github_version = components[-1][:-len('.zip')]
        else:
            # Release tag is a path component.
            assert (components[5] == 'releases')
            github_version = components[7]
        # If it's not a GH hash, it's a tagged release.
        tagged_release = len(github_version) != 40
        return GitHubRelease(
            organization=components[3],
            project=components[4],
            version=github_version,
            tagged=tagged_release)
    return None


# Render a CSV table given a list of table headers, widths and list of rows
# (each a list of strings).
def csv_table(headers, widths, rows):
    csv_rows = '\n  '.join(', '.join(row) for row in rows)
    return f'''.. csv-table::
  :header: {', '.join(headers)}
  :widths: {', '.join(str(w) for w in widths) }

  {csv_rows}

'''


# Anonymous external RST link for a given URL.
def rst_link(text, url):
    return f'`{text} <{url}>`__'


# NIST CPE database search URL for a given CPE.
def nist_cpe_url(cpe):
    encoded_cpe = urllib.parse.quote(cpe)
    return f'https://nvd.nist.gov/vuln/search/results?form_type=Advanced&results_type=overview&query={encoded_cpe}&search_type=all'


# Render version strings human readable.
def render_version(version):
    # Heuristic, almost certainly a git SHA
    if len(version) == 40:
        # Abbreviate git SHA
        return version[:7]
    return version


def render_title(title):
    underline = '-' * len(title)
    return f'\n{title}\n{underline}\n\n'


# Determine the version link URL. If it's GitHub, use some heuristics to figure
# out a release tag link, otherwise point to the GitHub tree at the respective
# SHA. Otherwise, return the tarball download.
def get_version_url(metadata):
    # Figure out if it's a GitHub repo.
    github_release = get_github_release_from_urls(metadata['urls'])
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


def csv_row(dep):
    return [dep.name, dep.version, dep.release_date, dep.cpe, dep.license]


def main():
    repository_locations = json.loads(pathlib.Path(sys.argv[1]).read_text())
    output_filename = sys.argv[2]
    generated_rst_dir = os.path.dirname(output_filename)
    security_rst_root = os.path.join(generated_rst_dir, "intro/arch_overview/security")

    pathlib.Path(security_rst_root).mkdir(parents=True, exist_ok=True)

    Dep = namedtuple('Dep', ['name', 'sort_name', 'version', 'cpe', 'release_date', 'license'])
    use_categories = defaultdict(lambda: defaultdict(list))
    # Bin rendered dependencies into per-use category lists.
    for k, v in repository_locations.items():
        cpe = v.get('cpe', '')
        if cpe == 'N/A':
            cpe = ''
        if cpe:
            cpe = rst_link(cpe, nist_cpe_url(cpe))
        project_name = v['project_name']
        project_url = v['project_url']
        name = rst_link(project_name, project_url)
        version = rst_link(render_version(v['version']), get_version_url(v))
        release_date = v['release_date']
        license = v.get('license', '')
        if license:
            license_url = v.get('license_url', '')
            if license_url:
                license = rst_link(license, license_url)
        dep = Dep(name, project_name.lower(), version, cpe, release_date, license)
        for category in v['use_category']:
            for ext in v.get('extensions', ['core']):
                use_categories[category][ext].append(dep)

    # Generate per-use category RST with CSV tables.
    for category, exts in use_categories.items():
        content = f"External dependencies: ``{category or 'core'}``"
        content += f"\n{'=' * len(content)}\n\n"
        for ext_name, deps in sorted(exts.items()):
            if not deps:
                continue
            if ext_name != 'core':
                content += render_title(ext_name)
            output_path = pathlib.Path(security_rst_root, f'external_dep_{category}.rst')
            content += csv_table(['Name', 'Version', 'Release date', 'CPE', 'License'],
                                 [2, 1, 1, 2, 1],
                                 [csv_row(dep) for dep in sorted(deps, key=lambda d: d.sort_name)])
        output_path.write_text(content)

    with tarfile.open(output_filename, "w:gz") as tar:
        tar.add(generated_rst_dir, arcname=".")


if __name__ == '__main__':
    sys.exit(main())
