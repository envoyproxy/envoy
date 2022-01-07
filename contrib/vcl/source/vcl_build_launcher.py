#!/usr/bin/python

# Launcher for building vcl

import os
import subprocess
import sys


def main():
    """ VCL builder script """

    # find path to helper script
    script_path = os.path.dirname(os.path.abspath(sys.argv[0]))
    vcl_build = f"{script_path}/{sys.argv[1]}"

    # find path to vpp/vcl source code
    base_path = os.path.dirname(os.path.abspath(sys.argv[1]))
    vpp_path = f"{base_path}/external/com_github_fdio_vpp_vcl"

    # find path to dst folder
    dst_path = os.path.dirname(os.path.abspath(sys.argv[2]))

    # build vcl
    subprocess.run([vcl_build, vpp_path, dst_path])


if __name__ == "__main__":
    main()
