
def parse_cve_cpe(cpe_string):
  (cpe_string // "" | split(":")) as $parts
  | {
      part:    ($parts[2] // "*"),
      vendor:  ($parts[3] // "*"),
      product: ($parts[4] // "*"),
      version: ($parts[5] // "*")
    };

def parse_deps(deps):
  deps
  | with_entries(
        select(.value.cpe != null and .value.cpe != "N/A")
        | .value
        |= (parse_cve_cpe(.cpe)) as $cc
        | {
          release_date,
          version,
          cpe: {
              match: .cpe,
              part: $cc.part,
              vendor: $cc.vendor,
              product: $cc.product,
              version: $cc.version
            }
        }
      );
