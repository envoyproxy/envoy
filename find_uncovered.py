#!/usr/bin/env python3
import json
import sys
import os

def get_coverage(node, path=""):
    results = {}
    if "children" in node:
        for name, child in node["children"].items():
            results.update(get_coverage(child, os.path.join(path, name) if path else name))
    elif "coverage" in node:
        # Line numbers are 1-indexed; index in array represents (line_number - 1)
        uncovered = [i + 1 for i, val in enumerate(node["coverage"]) if val == 0]
        results[path] = {
            "uncovered": uncovered,
            "percent": node.get("coveragePercent", 0),
        }
    return results

def main():
    covdir_path = "generated/coverage/covdir"
    if len(sys.argv) > 1:
        covdir_path = sys.argv[1]

    if not os.path.exists(covdir_path):
        print(f"Error: {covdir_path} not found. Did you run the coverage script?")
        sys.exit(1)

    with open(covdir_path, "r") as f:
        data = json.load(f)

    all_coverage = get_coverage(data)
    
    # Filter for ext_authz relevant files
    targets = ["ext_authz"]
    
    print(f"{'File Path':<70} | {'Coverage':<10} | {'Uncovered Lines'}")
    print("-" * 100)
    
    for file_path in sorted(all_coverage.keys()):
        if any(t in file_path for t in targets):
            info = all_coverage[file_path]
            uncovered_str = ", ".join(map(str, info['uncovered'])) if info['uncovered'] else "All covered"
            print(f"{file_path:<70} | {info['percent']:>8.1f}% | {uncovered_str}")

if __name__ == "__main__":
    main()
