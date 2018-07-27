
    bazel build //source/exe:envoy-static
    bazel-bin/source/exe/envoy-static --config-path ~/Downloads/envoy_10k_clusters_10k_virtual_hosts.json 2>&1 | tee ~/Downloads/stdout
    uniq_stats.py --stdout_path ~/Downloads/stdout
