#!/usr/bin/env bash

BAZELRC_FILE=~/.bazelrc bazel/setup_clang.sh /opt/llvm
echo "build --config=clang" >> user.bazelrc

# Ideally we want this line so bazel doesn't pollute things outside of the devcontainer, but some of
# API tooling (proto_sync) depends on symlink like bazel-bin.
# TODO(lizan): Fix API tooling and enable this again
#echo "build --symlink_prefix=/" >> ~/.bazelrc

[[ -n "${BUILD_DIR}" ]] && sudo chown -R "$(id -u):$(id -g)" "${BUILD_DIR}"

# Install dependencies for reverse-xds demo
echo "Setting up reverse-xDS demo dependencies..."

# Install Python dependencies (legacy)
if [[ -f "examples/reverse-xds/requirements.txt" ]]; then
    pip3 install --user -r examples/reverse-xds/requirements.txt
    echo "✅ Python dependencies installed"
else
    echo "ℹ️  requirements.txt not found, skipping Python dependency installation"
fi

# Set Go environment to avoid toolchain issues
export GOTOOLCHAIN=local

# Test Go installation and install Go dependencies for management server
if command -v go &> /dev/null; then
    echo "🚀 Go $(go version | cut -d' ' -f3) installed successfully"
    
    # Install Go dependencies for management server
    if [[ -d "examples/reverse-xds/server" && -f "examples/reverse-xds/server/go.mod" ]]; then
        echo "📦 Installing Go dependencies for management server..."
        cd examples/reverse-xds/server
        GOTOOLCHAIN=local go mod download
        echo "✅ Go dependencies installed"
        cd - > /dev/null
    fi
else
    echo "❌ Go installation failed"
fi
