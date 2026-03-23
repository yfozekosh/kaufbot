#!/bin/bash
#
# Install dev tools (clang-tidy, clang-format, cppcheck, lcov)
# either system-wide or locally into .tools/
#
# Usage:
#   ./tools/install.sh           # install system-wide (requires sudo/dnf)
#   ./tools/install.sh --local   # install locally into .tools/
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
LOCAL_DIR="${PROJECT_DIR}/.tools"

LLVM_VERSION="19.1.7"
CPPCHECK_VERSION="2.16.0"
LCOV_VERSION="2.0"

ARCH="$(uname -m)"
case "$ARCH" in
    x86_64) LLVM_ARCH="x86_64" ;;
    aarch64) LLVM_ARCH="aarch64" ;;
    *) echo "Unsupported architecture: $ARCH"; exit 1 ;;
esac

# ── Helpers ───────────────────────────────────────────────────────────────

have() { command -v "$1" &>/dev/null; }

download() {
    local url="$1" dest="$2"
    if [ -f "$dest" ]; then
        echo "  Already cached: $dest"
        return
    fi
    echo "  Downloading: $url"
    if have curl; then
        curl -fSL --progress-bar -o "$dest" "$url"
    elif have wget; then
        wget -q --show-progress -O "$dest" "$url"
    else
        echo "ERROR: need curl or wget"
        exit 1
    fi
}

# ── System install ────────────────────────────────────────────────────────

install_system() {
    echo "=== Installing tools system-wide ==="

    if have dnf; then
        sudo dnf install -y clang-tools-extra cppcheck lcov
    elif have apt; then
        sudo apt-get update
        sudo apt-get install -y clang-tidy clang-format cppcheck lcov
    elif have pacman; then
        sudo pacman -S --noconfirm clang cppcheck lcov
    else
        echo "ERROR: unsupported package manager. Use --local instead."
        exit 1
    fi

    echo ""
    echo "Installed:"
    clang-tidy --version | head -1
    clang-format --version | head -1
    cppcheck --version
    lcov --version | head -1
    echo ""
    echo "Done. Run 'cmake -DBUILD_TESTS=ON -S . -B build' to regenerate."
}

# ── Local install ─────────────────────────────────────────────────────────

install_llvm_local() {
    local bin_dir="$LOCAL_DIR/bin"
    local lib_dir="$LOCAL_DIR/lib"

    if [ -x "$bin_dir/clang-tidy" ] && [ -x "$bin_dir/clang-format" ]; then
        echo "  clang-tidy/clang-format already installed locally"
        return
    fi

    echo "--- Installing LLVM ${LLVM_VERSION} (clang-tidy + clang-format) ---"

    local tarball="clang+llvm-${LLVM_VERSION}-${LLVM_ARCH}-linux-gnu-ubuntu-22.04.tar.xz"
    local url="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${tarball}"
    local cache_dir="$LOCAL_DIR/cache"

    mkdir -p "$cache_dir" "$bin_dir" "$lib_dir"
    download "$url" "$cache_dir/$tarball"

    echo "  Extracting..."
    local extract_dir="$cache_dir/llvm-extract"
    mkdir -p "$extract_dir"
    tar -xf "$cache_dir/$tarball" -C "$extract_dir" --strip-components=1 \
        --wildcards '*/bin/clang-tidy' '*/bin/clang-format' \
                    '*/lib/libclang-cpp*' '*/lib/libLLVM*'

    cp "$extract_dir/bin/clang-tidy" "$bin_dir/" 2>/dev/null || true
    cp "$extract_dir/bin/clang-format" "$bin_dir/" 2>/dev/null || true
    cp "$extract_dir"/lib/libclang-cpp* "$lib_dir/" 2>/dev/null || true
    cp "$extract_dir"/lib/libLLVM* "$lib_dir/" 2>/dev/null || true

    chmod +x "$bin_dir/clang-tidy" "$bin_dir/clang-format"
    rm -rf "$extract_dir"

    echo "  Installed: clang-tidy, clang-format -> .tools/bin/"
}

install_cppcheck_local() {
    if [ -x "$LOCAL_DIR/bin/cppcheck" ]; then
        echo "  cppcheck already installed locally"
        return
    fi

    echo "--- Installing cppcheck ${CPPCHECK_VERSION} from source ---"

    local cache_dir="$LOCAL_DIR/cache"
    local src_dir="$cache_dir/cppcheck-${CPPCHECK_VERSION}"
    local url="https://github.com/danmar/cppcheck/archive/refs/tags/${CPPCHECK_VERSION}.tar.gz"

    mkdir -p "$cache_dir" "$LOCAL_DIR/bin"
    download "$url" "$cache_dir/cppcheck-${CPPCHECK_VERSION}.tar.gz"

    if [ ! -d "$src_dir" ]; then
        tar -xf "$cache_dir/cppcheck-${CPPCHECK_VERSION}.tar.gz" -C "$cache_dir"
    fi

    echo "  Building..."
    make -j"$(nproc)" -C "$src_dir" FILESDIR="$LOCAL_DIR/share/cppcheck" HAVE_RULES=no

    cp "$src_dir/cppcheck" "$LOCAL_DIR/bin/"
    chmod +x "$LOCAL_DIR/bin/cppcheck"
    rm -rf "$src_dir"

    echo "  Installed: cppcheck -> .tools/bin/"
}

install_lcov_local() {
    if [ -x "$LOCAL_DIR/bin/lcov" ]; then
        echo "  lcov already installed locally"
        return
    fi

    echo "--- Installing lcov ${LCOV_VERSION} ---"

    local cache_dir="$LOCAL_DIR/cache"
    local url="https://github.com/linux-test-project/lcov/releases/download/v${LCOV_VERSION}/lcov-${LCOV_VERSION}.tar.gz"

    mkdir -p "$cache_dir" "$LOCAL_DIR/bin"
    download "$url" "$cache_dir/lcov-${LCOV_VERSION}.tar.gz"

    local extract_dir="$cache_dir/lcov-extract"
    mkdir -p "$extract_dir"
    tar -xf "$cache_dir/lcov-${LCOV_VERSION}.tar.gz" -C "$extract_dir" --strip-components=1

    # lcov and genhtml are Perl scripts — just copy them
    cp "$extract_dir/bin/lcov" "$LOCAL_DIR/bin/"
    cp "$extract_dir/bin/genhtml" "$LOCAL_DIR/bin/"
    chmod +x "$LOCAL_DIR/bin/lcov" "$LOCAL_DIR/bin/genhtml"

    # Patch shebang for portability
    sed -i '1s|.*|#!/usr/bin/env perl|' "$LOCAL_DIR/bin/lcov"
    sed -i '1s|.*|#!/usr/bin/env perl|' "$LOCAL_DIR/bin/genhtml"

    rm -rf "$extract_dir"

    echo "  Installed: lcov, genhtml -> .tools/bin/"
}

write_env_script() {
    cat > "$LOCAL_DIR/env.sh" << 'ENVEOF'
#!/bin/bash
# Source this file to add project-local tools to PATH
# Usage: source .tools/env.sh
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PATH="$SCRIPT_DIR/bin:$PATH"
if [ -d "$SCRIPT_DIR/lib" ]; then
    export LD_LIBRARY_PATH="$SCRIPT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
ENVEOF
    chmod +x "$LOCAL_DIR/env.sh"
}

install_local() {
    echo "=== Installing tools locally into .tools/ ==="
    echo ""

    mkdir -p "$LOCAL_DIR/bin" "$LOCAL_DIR/cache"

    install_llvm_local
    install_cppcheck_local
    install_lcov_local
    write_env_script

    echo ""
    echo "=== Installed tools ==="
    (
        source "$LOCAL_DIR/env.sh"
        echo "clang-tidy:  $(clang-tidy --version 2>&1 | head -1)"
        echo "clang-format: $(clang-format --version 2>&1 | head -1)"
        echo "cppcheck:    $(cppcheck --version 2>&1)"
        echo "lcov:        $(lcov --version 2>&1 | head -1)"
        echo "genhtml:     $(genhtml --version 2>&1 | head -1)"
    )
    echo ""
    echo "To use: source .tools/env.sh"
    echo "Or add to your shell profile."
}

# ── Main ──────────────────────────────────────────────────────────────────

case "${1:-}" in
    --local)
        install_local
        ;;
    --help|-h)
        echo "Usage: $0 [--local]"
        echo "  (no args)   Install system-wide via package manager"
        echo "  --local     Install into .tools/ (no sudo needed)"
        ;;
    *)
        install_system
        ;;
esac
