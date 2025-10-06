import "ansi" as Ansi;
import "cve_utils" as Utils;

def matched_dependencies(cve):
  .
  | cve as $cve
  | cve.matched[]
  | "Dep: \(Ansi::green)\(Ansi::bold)\(.dep.name)@\(.dep.version.string)\(Ansi::reset) \(Ansi::green)(\(.dep.release_date)\(Ansi::reset))

  Severity: \(Utils::get_severity($cve.cve.metrics))
  Published: \(Utils::to_date_string($cve.cve.published))
  Version: \(.cpe.cpe.version)
  Start (including/excluding): \(.cpe.versions.start_inc // "")/\(.cpe.versions.start_exc // "")
  End (including/excluding): \(.cpe.versions.end_inc // "")/\(.cpe.versions.end_exc // "")

  ---------------------------------

  \($cve.cve.descriptions[0].value)

  ";

def cve_report(cve):
  "================ \(Ansi::red)\(Ansi::bold)\(Ansi::underline)\(cve.id)\(Ansi::reset) ================
  \(matched_dependencies(cve))
  ";

def summary(matches):
  .
  | "
  ==========================================
  \(Ansi::red)\(Ansi::bold)\(matches | length) potential CVE vulnerabilities found\(Ansi::reset)";

def report(matches):
  matches
  | matches as $matches
  | map(cve_report(.))
  | join("\n") + summary($matches);

report(.)
