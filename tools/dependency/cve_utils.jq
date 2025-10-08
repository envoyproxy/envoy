import "version" as Version;

def to_date_string(datestring):
  # convert cve date string to `%Y-%m-%d`
  .
  | datestring
  | sub("\\.[0-9]+";"")
  | "\(.)Z"
  | fromdateiso8601
  | strftime("%Y-%m-%d");

def deps_list(deps):
  # convert deps dict to a list of dicts with dep name as key
  .
  | [deps | to_entries[] | {name: .key} + .value];

def match_cpe_tag_version(cpe_version; dep_version):
  # matches the cpe tag version if its set
  .
  | cpe_version == "*"
    or cpe_version == dep_version;

def match_cpe_tag(cpe; dep):
  # return bool on whether tag matches for cpe and dep
  .
  | cpe.vendor == dep.cpe.vendor
    and cpe.product == dep.cpe.product
    and match_cpe_tag_version(cpe.version; dep.version);

def parse_cpe_versions(versions):
  # parse the cpe versions into semantic parts
  .
  | {end_inc: Version::parse(versions.end_inc),
     end_exc: Version::parse(versions.end_exc),
     start_inc: Version::parse(versions.start_inc),
     start_exc: Version::parse(versions.start_exc)};

def match_cpe_version(cpe; dep):
  # return bool of whether the dep version is outside defined version bounds
  .
  | parse_cpe_versions(cpe.versions) as $v
  | ($v.start_inc != null and Version::lt(dep.version; $v.start_inc))
     or ($v.start_exc != null and Version::lte(dep.version; $v.start_exc))
     or ($v.end_inc != null and Version::gt(dep.version; $v.end_inc))
     or ($v.end_exc != null and Version::gte(dep.version; $v.end_exc))
  | not;

def updated_since_cve(cpe; dep):
  .
  | dep.release_date > to_date_string(cpe.published);

def match_dep_cpe(cpe; dep):
  # first match the tag - if its one we are interested in then
  # check the versions where possible, otherwise (ie hash versions) check
  # the date
  .
  | match_cpe_tag(cpe.cpe; dep)
    and match_cpe_version(cpe; dep)
    and (updated_since_cve(cpe; dep) | not);

def matching_deps(cpes; deps):
  # for a given cpe list match deps against it
  .
  | [cpes[]? as $cpe
    | deps[]
    | . as $dep
    | select(match_dep_cpe($cpe; $dep))
    | {cpe: $cpe, dep: $dep}];

def parse_cpe_tag(cpe):
  # turn tag string to dict for matching
  .
  | cpe
  | split(":")
  | {
      part: (.[2] // "*"),
      vendor: (.[3] // "*"),
      product: (.[4] // "*"),
      version: (.[5] // "*")
    };

def pre_parse_cpe_versions(cpe):
  # this avoids the regex/version parsing until the cpe is otherwise matched
  .
  | {end_inc: cpe.versionEndIncluding,
     end_exc: cpe.versionEndExcluding,
     start_inc: cpe.versionStartIncluding,
     start_exc: cpe.versionStartExcluding};

def parse_cve_cpe(cpe; cve):
  # minimal representation of the cpe for matching
  .
  | {cpe: parse_cpe_tag(cpe.criteria),
     versions: pre_parse_cpe_versions(cpe),
     published: cve.cve.published};

def get_severity(metrics):
  if metrics.cvssMetricV31 then
    metrics.cvssMetricV31[0].cvssData.baseSeverity
  elif metrics.cvssMetricV30 then
    metrics.cvssMetricV30[0].cvssData.baseSeverity
  else
    "UNKNOWN"
  end;

##

def parse_deps(deps):
  .
  | deps
  | with_entries(
      select(.value.cpe != null and .value.cpe != "N/A")
      | .value
      |= parse_cpe_tag(.cpe) as $cc
      | {
        release_date,
        version: Version::parse(.version),
        cpe: {
            match: .cpe,
            part: $cc.part,
            vendor: $cc.vendor,
            product: $cc.product,
            version: $cc.version
          }
        }
      );

def iterate_cves(cves; deps; ignored):
  .
  | [cves[]
    | . as $cve
    | select(ignored | index($cve.id) | not)
    | [.cve.configurations[]?.nodes[]?.cpeMatch[]?
        | parse_cve_cpe(.; $cve)]
      | matching_deps(.; deps)
      | select(. | length > 0)
      | {id: $cve.cve.id, matched: ., cve: $cve.cve}];
