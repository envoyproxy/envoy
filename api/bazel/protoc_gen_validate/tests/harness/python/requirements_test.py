import configparser

# There's two sets of requirements relevant for python. The first set in requirements.txt is installed
# during the Docker build and is used for linting, building, and uploading the PGV python package to PyPI.
#
# The other set is in the install_requires section of setup.cfg. This is what's needed to use the package.
#
# We use pip_install from @rules_python to install these requirements in order to test the package. Unfortunately:
# - pip_install can't handle setup.cfg directly, it wants a file containing a simple list
# - as a bazel repository_rule, pip_install won't accept generated files as input so we can't autogen
#   this simpler file out of setup.cfg as part of bazel build.
#
# So instead here we just check that requirements.in matches what's in install_requires of setup.cfg.


with open('python/requirements.in', 'r') as reqs:
    lines = reqs.readlines()
    requirements_dot_in_set = {line.strip('\n') for line in lines if line.strip() and not line.startswith("#")}

config = configparser.ConfigParser()
config.read('python/setup.cfg')
setup_dot_cfg_set = {line for line in config['options']['install_requires'].split('\n') if line and not line.startswith("#")}

assert requirements_dot_in_set == setup_dot_cfg_set
