#!/usr/bin/env bash

set -eo pipefail


skopeo_copy () {
    local images tempdir
    read -ra images <<< "${1}"
    tempdir=$(mktemp -d)
    for image in "${images[@]}"; do
        src_name="$(echo "${image}" | cut -d: -f1)"
        dest_name="$(echo "${image}" | cut -d: -f2)"
        src="oci-archive:${RUNNER_TEMP}/container/build_images/${src_name}.amd64.tar"
        dest_file="${tempdir}/${dest_name}.amd64.tar"
        dest="docker-archive:${dest_file}:envoyproxy/envoy:${dest_name}"
        echo "Copy image: ${src} ${dest}"
        skopeo copy -q "${src}" "${dest}"
        echo "Load docker archive: ${dest_file}"
        docker load -i "${dest_file}"
        rm -f "${dest_file}"
    done
}
