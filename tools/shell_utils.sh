source_venv() {
  VENV_DIR=$1
  if [[ "$VIRTUAL_ENV" == "" ]]; then
    if [[ ! -d "${VENV_DIR}"/venv ]]; then
      virtualenv "${VENV_DIR}"/venv --no-site-packages --python=python2.7
    fi
    source "${VENV_DIR}"/venv/bin/activate
  else
    echo "Found existing virtualenv"
  fi
}
