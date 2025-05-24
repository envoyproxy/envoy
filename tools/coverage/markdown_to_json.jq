split("\n")[2:]
| map(select(length > 0 and test("^\\|")))
| map(
    split("|")
    | map(gsub("^\\s+|\\s+$"; ""))
    | select(length >= 4)
    | select(.[1] != "" and .[1] != "File" and .[1] != "-")
    | {
        key: .[1],
        value: {
          coverage: .[2],
          coverage_percent: (
            if .[2] | test("^[0-9.]+%$") then
              .[2] | rtrimstr("%") | tonumber
            else
              0
            end
          ),
          lines: .[3],
          lines_covered: (
            if .[3] | test("^[0-9]+ / [0-9]+$") then
              .[3] | split(" / ")[0] | tonumber
            else
              0
            end
          ),
          lines_total: (
            if .[3] | test("^[0-9]+ / [0-9]+$") then
              .[3] | split(" / ")[1] | tonumber
            else
              0
            end
          ),
          uncovered_lines: .[4]
        }
      }
  )
| map(select(.key != null))
| from_entries
| . as $files
| {
    files: $files,
    per_directory_coverage: (
      # Get all unique directory paths that we need to calculate coverage for
      # This includes all parent directories of all files
      ($files
        | to_entries
        | map(.key | split("/")[:-1] | . as $parts | range(1; length + 1) | $parts[0:.] | join("/"))
        | flatten
        | unique) as $all_dirs
      # For each directory, calculate coverage from all files under it (recursively)
      | $all_dirs
      | map(. as $current_dir |
          {
            key: $current_dir,
            value: (
              $files
              | to_entries
              # Select only files that are under this directory (recursively)
              # The file must start with the directory path followed by a slash
              | map(select(.key | startswith($current_dir + "/")))
              | if length > 0 then
                  {
                    lines_covered: (map(.value.lines_covered) | add),
                    lines_total: (map(.value.lines_total) | add),
                    coverage_percent: (
                      if (map(.value.lines_total) | add) > 0 then
                        (((map(.value.lines_covered) | add) / (map(.value.lines_total) | add) * 1000) | round) / 10
                      else
                        0
                      end
                    )
                  }
                else
                  {
                    lines_covered: 0,
                    lines_total: 0,
                    coverage_percent: 0
                  }
                end
            )
          }
        )
      | from_entries
    ),
    summary: {
      total_files: ($files | to_entries | length),
      total_lines: ($files | to_entries | map(.value.lines_total) | add // 0),
      lines_covered: ($files | to_entries | map(.value.lines_covered) | add // 0),
      coverage_percent: (
        if ($files | to_entries | map(.value.lines_total) | add // 0) > 0 then
          ((($files | to_entries | map(.value.lines_covered) | add) / ($files | to_entries | map(.value.lines_total) | add) * 1000) | round) / 10
        else 0
        end
      )
    }
  }
