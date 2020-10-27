#!/usr/bin/env python3

# Scan for any external dependencies that were last updated before known CVEs
# (and near relatives). We also try a fuzzy match on version information.

from collections import defaultdict, namedtuple
import datetime as dt
import gzip
import json
import re
import sys
import textwrap
import urllib.request

import utils as dep_utils

# These CVEs are false positives for the match heuristics. An explanation is
# required when adding a new entry to this list as a comment.
IGNORES_CVES = set([
    # Node.js issue unrelated to http-parser (napi_ API implementation).
    'CVE-2020-8174',
    # Node.js HTTP desync attack. Request smuggling due to CR and hyphen
    # conflation in llhttp
    # (https://github.com/nodejs/llhttp/commit/9d9da1d0f18599ceddd8f484df5a5ad694d23361).
    # This was a result of using llparse's toLowerUnsafe() for header keys.
    # http-parser uses a TOKEN method that doesn't have the same issue for
    # header fields.
    'CVE-2020-8201',
    # Node.js issue unrelated to http-parser. This is a DoS due to a lack of
    # request/connection timeouts, see
    # https://github.com/nodejs/node/commit/753f3b247a.
    'CVE-2020-8251',
    # Node.js issue unrelated to http-parser (libuv).
    'CVE-2020-8252',
    # Fixed via the nghttp2 1.41.0 bump in Envoy 8b6ea4.
    'CVE-2020-11080',
])

# Subset of CVE fields that are useful below.
Cve = namedtuple(
    'Cve',
    ['id', 'description', 'cpes', 'score', 'severity', 'published_date', 'last_modified_date'])


class Cpe(namedtuple('CPE', ['part', 'vendor', 'product', 'version'])):
  '''Model a subset of CPE fields that are used in CPE matching.'''

  @classmethod
  def FromString(cls, cpe_str):
    assert (cpe_str.startswith('cpe:2.3:'))
    components = cpe_str.split(':')
    assert (len(components) >= 6)
    return cls(*components[2:6])

  def __str__(self):
    return f'cpe:2.3:{self.part}:{self.vendor}:{self.product}:{self.version}'

  def VendorNormalized(self):
    '''Return a normalized CPE where only part and vendor are significant.'''
    return Cpe(self.part, self.vendor, '*', '*')


def ParseCveJson(cve_json, cves, cpe_revmap):
  '''Parse CVE JSON dictionary.

  Args:
    cve_json: a NIST CVE JSON dictionary.
    cves: dictionary mapping CVE ID string to Cve object (output).
    cpe_revmap: a reverse map from vendor normalized CPE to CVE ID string.
  '''

  # This provides an over-approximation of possible CPEs affected by CVE nodes
  # metadata; it traverses the entire AND-OR tree and just gathers every CPE
  # observed. Generally we expect that most of Envoy's CVE-CPE matches to be
  # simple, plus it's interesting to consumers of this data to understand when a
  # CPE pops up, even in a conditional setting.
  def GatherCpes(nodes, cpe_set):
    for node in nodes:
      for cpe_match in node.get('cpe_match', []):
        cpe_set.add(Cpe.FromString(cpe_match['cpe23Uri']))
      GatherCpes(node.get('children', []), cpe_set)

  for cve in cve_json['CVE_Items']:
    cve_id = cve['cve']['CVE_data_meta']['ID']
    description = cve['cve']['description']['description_data'][0]['value']
    cpe_set = set()
    GatherCpes(cve['configurations']['nodes'], cpe_set)
    if len(cpe_set) == 0:
      continue
    cvss_v3_score = cve['impact']['baseMetricV3']['cvssV3']['baseScore']
    cvss_v3_severity = cve['impact']['baseMetricV3']['cvssV3']['baseSeverity']

    def ParseCveDate(date_str):
      assert (date_str.endswith('Z'))
      return dt.date.fromisoformat(date_str.split('T')[0])

    published_date = ParseCveDate(cve['publishedDate'])
    last_modified_date = ParseCveDate(cve['lastModifiedDate'])
    cves[cve_id] = Cve(cve_id, description, cpe_set, cvss_v3_score, cvss_v3_severity,
                       published_date, last_modified_date)
    for cpe in cpe_set:
      cpe_revmap[str(cpe.VendorNormalized())].add(cve_id)
  return cves, cpe_revmap


def DownloadCveData(urls):
  '''Download NIST CVE JSON databases from given URLs and parse.

  Args:
    urls: a list of URLs.
  Returns:
    cves: dictionary mapping CVE ID string to Cve object (output).
    cpe_revmap: a reverse map from vendor normalized CPE to CVE ID string.
  '''
  cves = {}
  cpe_revmap = defaultdict(set)
  for url in urls:
    print(f'Loading NIST CVE database from {url}...')
    with urllib.request.urlopen(url) as request:
      with gzip.GzipFile(fileobj=request) as json_data:
        ParseCveJson(json.loads(json_data.read()), cves, cpe_revmap)
  return cves, cpe_revmap


def FormatCveDetails(cve, deps):
  formatted_deps = ', '.join(sorted(deps))
  wrapped_description = '\n  '.join(textwrap.wrap(cve.description))
  return f'''
  CVE ID: {cve.id}
  CVSS v3 score: {cve.score}
  Severity: {cve.severity}
  Published date: {cve.published_date}
  Last modified date: {cve.last_modified_date}
  Dependencies: {formatted_deps}
  Description: {wrapped_description}
  Affected CPEs:
  ''' + '\n  '.join(f'- {cpe}' for cpe in cve.cpes)


FUZZY_DATE_RE = re.compile('(\d{4}).?(\d{2}).?(\d{2})')
FUZZY_SEMVER_RE = re.compile('(\d+)[:\.\-_](\d+)[:\.\-_](\d+)')


def RegexGroupsMatch(regex, lhs, rhs):
  '''Do two strings match modulo a regular expression?

  Args:
    regex: regular expression
    lhs: LHS string
    rhs: RHS string
  Returns:
    A boolean indicating match.
  '''
  lhs_match = regex.search(lhs)
  if lhs_match:
    rhs_match = regex.search(rhs)
    if rhs_match and lhs_match.groups() == rhs_match.groups():
      return True
  return False


def CpeMatch(cpe, dep_metadata):
  '''Heuristically match dependency metadata against CPE.

  We have a number of rules below that should are easy to compute without having
  to look at the dependency metadata. In the future, with additional access to
  repository information we could do the following:
  - For dependencies at a non-release version, walk back through git history to
    the last known release version and attempt a match with this.
  - For dependencies at a non-release version, use the commit date to look for a
    version match where version is YYYY-MM-DD.

  Args:
    cpe: Cpe object to match against.
    dep_metadata: dependency metadata dictionary.
  Returns:
    A boolean indicating a match.
  '''
  dep_cpe = Cpe.FromString(dep_metadata['cpe'])
  dep_version = dep_metadata['version']
  # The 'part' and 'vendor' must be an exact match.
  if cpe.part != dep_cpe.part:
    return False
  if cpe.vendor != dep_cpe.vendor:
    return False
  # We allow Envoy dependency CPEs to wildcard the 'product', this is useful for
  # LLVM where multiple product need to be covered.
  if dep_cpe.product != '*' and cpe.product != dep_cpe.product:
    return False
  # Wildcard versions always match.
  if cpe.version == '*':
    return True
  # An exact version match is a hit.
  if cpe.version == dep_version:
    return True
  # Allow the 'release_date' dependency metadata to substitute for date.
  # TODO(htuch): Consider fuzzier date ranges.
  if cpe.version == dep_metadata['release_date']:
    return True
  # Try a fuzzy date match to deal with versions like fips-20190304 in dependency version.
  if RegexGroupsMatch(FUZZY_DATE_RE, dep_version, cpe.version):
    return True
  # Try a fuzzy semver match to deal with things like 2.1.0-beta3.
  if RegexGroupsMatch(FUZZY_SEMVER_RE, dep_version, cpe.version):
    return True
  # Fall-thru.
  return False


def CveMatch(cve, dep_metadata):
  '''Heuristically match dependency metadata against CVE.

  In general, we allow false positives but want to keep the noise low, to avoid
  the toil around having to populate IGNORES_CVES.

  Args:
    cve: Cve object to match against.
    dep_metadata: dependency metadata dictionary.
  Returns:
    A boolean indicating a match.
  '''
  wildcard_version_match = False
  # Consider each CPE attached to the CVE for a match against the dependency CPE.
  for cpe in cve.cpes:
    if CpeMatch(cpe, dep_metadata):
      # Wildcard version matches need additional heuristics unrelated to CPE to
      # qualify, e.g. last updated date.
      if cpe.version == '*':
        wildcard_version_match = True
      else:
        return True
  if wildcard_version_match:
    # If the CVE was published after the dependency was last updated, it's a
    # potential match.
    last_dep_update = dt.date.fromisoformat(dep_metadata['release_date'])
    if last_dep_update <= cve.published_date:
      return True
  return False


def CveScan(cves, cpe_revmap, cve_allowlist, repository_locations):
  '''Scan for CVEs in a parsed NIST CVE database.

  Args:
    cves: CVE dictionary as provided by DownloadCveData().
    cve_revmap: CPE-CVE reverse map as provided by DownloadCveData().
    cve_allowlist: an allowlist of CVE IDs to ignore.
    repository_locations: a dictionary of dependency metadata in the format
      described in api/bazel/external_deps.bzl.
  Returns:
    possible_cves: a dictionary mapping CVE IDs to Cve objects.
    cve_deps: a dictionary mapping CVE IDs to dependency names.
  '''
  possible_cves = {}
  cve_deps = defaultdict(list)
  for dep, metadata in repository_locations.items():
    cpe = metadata.get('cpe', 'N/A')
    if cpe == 'N/A':
      continue
    candidate_cve_ids = cpe_revmap.get(str(Cpe.FromString(cpe).VendorNormalized()), [])
    for cve_id in candidate_cve_ids:
      cve = cves[cve_id]
      if cve.id in cve_allowlist:
        continue
      if CveMatch(cve, metadata):
        possible_cves[cve_id] = cve
        cve_deps[cve_id].append(dep)
  return possible_cves, cve_deps


if __name__ == '__main__':
  # Allow local overrides for NIST CVE database URLs via args.
  urls = sys.argv[1:]
  if not urls:
    # We only look back a few years, since we shouldn't have any ancient deps.
    current_year = dt.datetime.now().year
    scan_years = range(2018, current_year + 1)
    urls = [
        f'https://nvd.nist.gov/feeds/json/cve/1.1/nvdcve-1.1-{year}.json.gz' for year in scan_years
    ]
  cves, cpe_revmap = DownloadCveData(urls)
  possible_cves, cve_deps = CveScan(cves, cpe_revmap, IGNORES_CVES, dep_utils.RepositoryLocations())
  if possible_cves:
    print('\nBased on heuristic matching with the NIST CVE database, Envoy may be vulnerable to:')
    for cve_id in sorted(possible_cves):
      print(f'{FormatCveDetails(possible_cves[cve_id], cve_deps[cve_id])}')
    sys.exit(1)
