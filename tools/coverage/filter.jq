# Extract coverage data from grcov's covdir JSON output with validation information

def round_to_precision(precision):
  . * pow(10; precision)
  | round / pow(10; precision);

def calculate_summary:
  {
    coverage_percent: (.coveragePercent | round_to_precision(1)),
    lines_covered: .linesCovered,
    lines_total: .linesTotal,
    threshold: (if $config[0] then ($config[0].thresholds.total // 0) else 0 end),
    threshold_reached: (.coveragePercent >= (if $config[0] then ($config[0].thresholds.total // 0) else 0 end))
  };

def get_default_threshold:
  if $config[0] then
    ($config[0].thresholds.per_directory // 0)
  else 0 end;

def directory_stats(path):
  {
    expected_percent: ((if $config[0] and $config[0].directories then $config[0].directories[path] else null end) // get_default_threshold),
    coverage_percent: (.coveragePercent | round_to_precision(1)),
    lines_covered: .linesCovered,
    lines_total: .linesTotal
  };

def extract_dirs(path):
  if .children then
    (if path != "" and .coveragePercent != null and .linesCovered != null and .linesTotal != null then
      {
        (path): directory_stats(path)
      }
     else empty end),
    # Recurse into all subdirectories
    (.children
     | to_entries[]
     | .key as $name
     | .value
     | if .children then
        extract_dirs(if path == "" then $name else path + "/\($name)" end)
      else empty end)
  else empty end;

def categorize_directories:
  . as $coverage_data
  | get_default_threshold as $default
  | {failed_coverage: [],
     low_coverage: [],
     excellent_coverage: [],
     high_coverage_adjustable: []
    } as $categories
  |
  # Process each source directory
  ($source_directories
   | split("\n")
   | map(select(. != ""))) as $dirs
  |
  reduce $dirs[] as $dir ($categories;
    ($coverage_data[$dir] // null) as $dir_data
    | if $dir_data then
      ($dir_data.expected_percent) as $threshold
      | ($dir_data.coverage_percent) as $coverage
      |
      if $coverage < $threshold then
        .failed_coverage += [{
          directory: $dir,
          coverage_percent: $coverage,
          threshold: $threshold
        }]
      else
        if $threshold < $default then
          # Directory has a configured exception (lower threshold)
          .low_coverage += [{
            directory: $dir,
            coverage_percent: $coverage,
            threshold: $threshold
          }]
          |
          # Check if significantly higher than configured threshold
          if $coverage > $threshold then
            .high_coverage_adjustable += [{
              directory: $dir,
              coverage_percent: $coverage,
              threshold: $threshold
            }]
          else . end
        elif $coverage >= $default then
          # Excellent coverage (meets default threshold)
          .excellent_coverage += [{
            directory: $dir,
            coverage_percent: $coverage
          }]
        else . end
      end
    else . end
  );

def should_process_directories:
  ($source_directories != "" and $source_directories != null)
    or ($config[0] and $config[0].directories and ($config[0].directories | length) > 0);

def generate_summary_message:
  . as $data
  | if $data.summary.threshold_reached then
      "Code coverage \(.summary.coverage_percent)% is good and higher than limit of \(.summary.threshold)%"
    else
      "Code coverage \(.summary.coverage_percent)% is lower than limit of \(.summary.threshold)%"
    end;

def generate_summary_failed:
  . as $data
  | (if ($data.validation.failed_coverage | length) > 0 then
       ["FAILED: Directories not meeting coverage thresholds:"]
         + ($data.validation.failed_coverage
             | map("  ✗ \(.directory): \(.coverage_percent | tostring)% (threshold: \(.threshold | tostring)%)"))
         + [""]
     else [] end)
  | join("\n");

def generate_summary_adjustable:
  . as $data
  | (if ($data.validation.high_coverage_adjustable | length) > 0 then
       ["WARNING: Coverage in the following directories may be adjusted up:"]
         + ($data.validation.high_coverage_adjustable
             | map("  ⬆  \(.directory): \(.coverage_percent | tostring)% (current threshold: \(.threshold | tostring)%)"))
         + [""]
     else [] end)
  | join("\n");

def generate_summary_excellent:
  . as $data
  | (if ($data.validation.excellent_coverage | length) > 0 then
       ["Directories with excellent coverage (>= \($data.default_threshold | tostring)%):"]
         + ($data.validation.excellent_coverage
             | map("  ✓ \(.directory): \(.coverage_percent | tostring)%"))
         + [""]
     else [] end)
  | join("\n");

def generate_summary_low:
  . as $data
  | (if ($data.validation.low_coverage | length) > 0 then
       ["Directories with known low coverage (meeting configured thresholds):"]
         + ($data.validation.low_coverage
             | map("  ⚠ \(.directory): \(.coverage_percent | tostring)% (configured threshold: \(.threshold | tostring)%)"))
         + [""]
     else [] end)
  | join("\n");

def generate_summary_adjust_message:
  . as $data
  | (if ($data.validation_summary.adjustable_count) > 0 then
       "Can be adjusted up: \($data.validation_summary.adjustable_count | tostring)"
     else "" end);

def generate_summary_directory_error:
  . as $data
  | (if ($data.validation_summary.all_passed | not) then
       "ERROR: Coverage check failed. Some directories are below their thresholds."
     else "" end);

def generate_summary_report:
  . as $data
  | "
================== Per-Directory Coverage Report ==================

\(generate_summary_excellent)
\(generate_summary_low)
\(generate_summary_adjustable)
\(generate_summary_failed)
==================================================================
Overall Coverage: \($data.summary.coverage_percent)%
Source directories checked: \($data.validation_summary.total_directories),
Failed: \($data.validation_summary.failed_count),
Low coverage (configured): \($data.validation_summary.low_coverage_count),
Excellent coverage: \($data.validation_summary.excellent_coverage_count)
\(generate_summary_adjust_message)
==================================================================

\(generate_summary_message)
\(generate_summary_directory_error)
";

# Main processing
{
  summary: calculate_summary,
  source_directories: (
      $source_directories
      | split("\n")
      | map(select(. != ""))),
  # Only calculate per_directory_coverage if we have directories to process
  per_directory_coverage: (
    if should_process_directories then
      [.children
       | to_entries[]
       | .key as $root_dir
       | .value
       | extract_dirs($root_dir)]
      | add // {}
    else
      {}
    end
  ),
  default_threshold: get_default_threshold,
  # Include coverage config if available
  coverage_config: (if $config[0] then $config[0] else null end)
}
|
# Add validation results only if we have directories to validate
. as $base
| if should_process_directories and $base.source_directories != [] then
    .per_directory_coverage as $per_dir
    | $base + {validation: ($per_dir | categorize_directories),
               validation_summary: (
                   $per_dir
                   | categorize_directories
                   | {total_directories: (
                          (.failed_coverage + .low_coverage + .excellent_coverage)
                          | map(.directory)
                          | unique
                          | length),
                      failed_count: (.failed_coverage | length),
                      low_coverage_count: (.low_coverage | length),
                      excellent_coverage_count: (.excellent_coverage | length),
                      adjustable_count: (.high_coverage_adjustable | length),
                      all_passed: ((.failed_coverage | length) == 0)})}
  else $base end
| . as $result
| $result + {summary_message: generate_summary_message}
| . as $result
| if $result.validation then
    $result + {summary_report: generate_summary_report}
  else . end
