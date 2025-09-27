import "version" as Version;

def deps_list(deps):
  # convert deps dict to a list of dicts with dep name as key
  .
  | [deps | to_entries[] | {name: .key} + .value];

def match_cpe_tag(cpe; dep):
  # return bool on whether tag matches for cpe and dep
  .
  | cpe.vendor == dep.cpe.vendor
    and cpe.product == dep.cpe.product
    and (cpe.version == dep.cpe.version
         or dep.cpe.version == "*");

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

def match_dep_cpe(cpe; dep):
  # first match the tag - if its one we are interested in then
  # check the versions where possible, otherwise (ie hash versions) check
  # the date
  .
  | match_cpe_tag(cpe.cpe; dep)
    and match_cpe_version(cpe; dep);

def matching_deps(cpes; deps):
  # for a given cpe list match deps against it
  .
  | [cpes[]? as $cpe
     | select(deps[] | match_dep_cpe($cpe; .))];

def parse_cpe_tag(cpe):
  # turn tag string to dict for matching
  .
  | (cpe // "" | split(":")) as $parts
  | {
      part: ($parts[2] // "*"),
      vendor: ($parts[3] // "*"),
      product: ($parts[4] // "*"),
      version: ($parts[5] // "*")
    };

def pre_parse_cpe_versions(cpe):
  # this avoids the regex/version parsing until the cpe is otherwise matched
  .
  | {end_inc: cpe.versionEndIncluding,
     end_exc: cpe.versionEndExcluding,
     start_inc: cpe.versionStartIncluding,
     start_exc: cpe.versionStartExcluding};

def parse_cve_cpe(cpe):
  # minimal representation of the cpe for matching
  .
  | {cpe: parse_cpe_tag(cpe.criteria),
     versions: pre_parse_cpe_versions(cpe)};

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
        | parse_cve_cpe(.)]
    | matching_deps(.; deps) as $matched
    | select($matched | length > 0)
    | {id: $cve.id, matched: $matched, cve: $cve.cve}];
