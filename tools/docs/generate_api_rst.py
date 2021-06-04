import os
import shutil
import sys
import tarfile


def main():
    proto_srcs = sys.argv[1]
    envoy_api_rst_files = sys.argv[1:-1]
    output_filename = sys.argv[-1]

    with open(proto_srcs) as f:
        envoy_api_protos = [
            f"{src.split('//')[1].replace(':', '/')}.rst" for src in f.read().split("\n") if src
        ]

    for rst_file_path in envoy_api_rst_files:
        if "pkg/envoy" not in rst_file_path:
            continue
        canonical = f"{rst_file_path.split('pkg/envoy/')[1]}"
        if f"envoy/{canonical}" not in envoy_api_protos:
            continue
        target = os.path.join("rst-out/api-v3", canonical)
        if not os.path.exists(os.path.dirname(target)):
            os.makedirs(os.path.dirname(target))
        shutil.copy(rst_file_path, target)

    with tarfile.open(output_filename, "w") as tar:
        tar.add("rst-out", arcname=".")


if __name__ == "__main__":
    main()
