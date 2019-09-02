source_venv() {
  VENV_DIR=$1
  if [[ "$VIRTUAL_ENV" == "" ]]; then
    if [[ ! -d "${VENV_DIR}"/venv ]]; then
      virtualenv "${VENV_DIR}"/venv --no-site-packages --python=python3
    fi
    source "${VENV_DIR}"/venv/bin/activate
  else
    echo "Found existing virtualenv"
  fi
}

python_venv() {
  SCRIPT_DIR=$(realpath "$(dirname "$0")")

  BUILD_DIR=build_tools
  VENV_DIR="$BUILD_DIR/$1"

  source_venv "$VENV_DIR"
  pip install -r "${SCRIPT_DIR}"/requirements.txt

  python3 "${SCRIPT_DIR}/$1.py" $*
}
