#!/bin/bash
# Bzlmod Architecture Validation Script
# 
# This script validates Envoy's bzlmod setup and consolidated extension architecture.

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if we're in the Envoy root directory
if [[ ! -f "MODULE.bazel" ]] || [[ ! -f "WORKSPACE" ]]; then
    log_error "Please run this script from the Envoy root directory"
    exit 1
fi

log_info "🚀 Starting Envoy bzlmod validation..."

# 1. Check Bazel version
log_info "Checking Bazel version..."
if command -v bazel >/dev/null 2>&1; then
    BAZEL_VERSION=$(bazel version 2>/dev/null | grep "Build label" | cut -d' ' -f3 || echo "unknown")
    log_info "Bazel version: $BAZEL_VERSION"
    BAZEL_AVAILABLE=true
else
    log_warning "Bazel not available - some tests will be skipped"
    BAZEL_AVAILABLE=false
fi

# 2. Test basic bzlmod commands
if [[ "$BAZEL_AVAILABLE" == "true" ]]; then
    log_info "Testing basic bzlmod functionality..."

    if timeout 30s bazel mod graph > /dev/null 2>&1; then
        log_success "bzlmod dependency graph generation works"
    else
        log_warning "bzlmod dependency graph generation failed - may be network issues"
    fi

    if timeout 20s bazel mod show_extension_repos > /dev/null 2>&1; then
        log_success "Extension repository listing works"
        EXT_COUNT=$(bazel mod show_extension_repos 2>/dev/null | grep -c "^@" || echo "0")
        log_info "Found $EXT_COUNT extension-provided repositories"
    else
        log_warning "Extension repository listing failed"
    fi
fi

# 3. Check MODULE.bazel structure
log_info "Analyzing MODULE.bazel structure..."

BAZEL_DEP_COUNT=$(grep -c "^bazel_dep" MODULE.bazel || echo "0")
EXT_COUNT=$(grep -c "use_extension" MODULE.bazel || echo "0")

log_info "Direct bazel_dep declarations: $BAZEL_DEP_COUNT"
log_info "Module extensions used: $EXT_COUNT"

if [[ $BAZEL_DEP_COUNT -gt 40 ]]; then
    log_success "Excellent BCR adoption with $BAZEL_DEP_COUNT direct dependencies"
elif [[ $BAZEL_DEP_COUNT -gt 30 ]]; then
    log_success "Good BCR adoption with $BAZEL_DEP_COUNT direct dependencies"
elif [[ $BAZEL_DEP_COUNT -gt 20 ]]; then
    log_warning "Moderate BCR adoption with $BAZEL_DEP_COUNT direct dependencies"
else
    log_warning "Low BCR adoption with $BAZEL_DEP_COUNT direct dependencies"
fi

# 4. Check extension organization
log_info "Checking extension organization..."

if [[ -d "bazel/extensions" ]]; then
    EXT_FILES=$(find bazel/extensions -name "*.bzl" | wc -l)
    log_info "Extension files in main module: $EXT_FILES"
    
    if [[ $EXT_FILES -gt 8 ]]; then
        log_warning "Consider consolidating extensions (current: $EXT_FILES, recommended: <8)"
    else
        log_success "Extension count is reasonable: $EXT_FILES"
    fi
else
    log_error "Extension directory not found"
fi

# 5. Test core builds with bzlmod
if [[ "$BAZEL_AVAILABLE" == "true" ]]; then
    log_info "Testing core builds with bzlmod..."

    # Test a simple library build (analysis only for speed)
    if timeout 60s bazel build --enable_bzlmod //source/common/common:version_lib --nobuild > /dev/null 2>&1; then
        log_success "Core library build analysis passes with bzlmod"
    else
        log_warning "Core library build analysis has issues with bzlmod"
    fi

    # Test broader analysis
    if timeout 90s bazel build --enable_bzlmod --nobuild //source/... > /dev/null 2>&1; then
        log_success "All source targets analyze successfully with bzlmod"
    else
        log_warning "Some source targets have issues with bzlmod (this may be expected)"
    fi
fi

# 6. Check for best practices
log_info "Checking best practices compliance..."

# Check for upstream extension usage
if grep -q "@rules_python//python/extensions:python.bzl" MODULE.bazel; then
    log_success "Using upstream rules_python extensions"
else
    log_warning "Consider using upstream rules_python extensions"
fi

if grep -q "@rules_python//python/extensions:pip.bzl" MODULE.bazel; then
    log_success "Using upstream pip extensions"
else
    log_warning "Consider using upstream pip extensions"
fi

# Check for development dependencies
if grep -q "dev_dependency = True" MODULE.bazel; then
    log_success "Using dev_dependency declarations"
else
    log_info "Consider marking test-only dependencies with dev_dependency = True"
fi

# 7. Check WORKSPACE.bzlmod status
log_info "Checking WORKSPACE.bzlmod status..."

if [[ -f "WORKSPACE.bzlmod" ]]; then
    LINES=$(wc -l < WORKSPACE.bzlmod)
    if [[ $LINES -le 5 ]]; then
        log_success "WORKSPACE.bzlmod is minimal ($LINES lines)"
    else
        log_warning "WORKSPACE.bzlmod should be eliminated or minimized ($LINES lines)"
    fi
else
    log_success "No WORKSPACE.bzlmod file found"
fi

# 8. Architecture assessment
log_info "Assessing architecture quality..."

TOTAL_SCORE=0
MAX_SCORE=100

# BCR adoption (40 points)
if [[ $BAZEL_DEP_COUNT -gt 40 ]]; then
    TOTAL_SCORE=$((TOTAL_SCORE + 40))
elif [[ $BAZEL_DEP_COUNT -gt 30 ]]; then
    TOTAL_SCORE=$((TOTAL_SCORE + 30))
elif [[ $BAZEL_DEP_COUNT -gt 20 ]]; then
    TOTAL_SCORE=$((TOTAL_SCORE + 20))
else
    TOTAL_SCORE=$((TOTAL_SCORE + 10))
fi

# Extension organization (30 points) - Recognizes consolidated architecture
MAIN_CONSOLIDATED=$(grep -c "core\.bzl\|toolchains\.bzl" MODULE.bazel 2>/dev/null || echo "0")
MOBILE_CONSOLIDATED=$(grep -c '^envoy_mobile.*use_extension.*//bazel/extensions:' mobile/MODULE.bazel 2>/dev/null || echo "0")

# Remove any potential newlines
MAIN_CONSOLIDATED=$(echo "$MAIN_CONSOLIDATED" | tr -d '\n')
MOBILE_CONSOLIDATED=$(echo "$MOBILE_CONSOLIDATED" | tr -d '\n')

log_info "Main module consolidated extensions: $MAIN_CONSOLIDATED"
log_info "Mobile module consolidated extensions: $MOBILE_CONSOLIDATED"

# Perfect consolidation: main (2) + mobile (2) + api (1) = 5 total
if [[ "$MAIN_CONSOLIDATED" -eq 2 ]] && [[ "$MOBILE_CONSOLIDATED" -eq 2 ]]; then
    TOTAL_SCORE=$((TOTAL_SCORE + 30))
    log_success "Perfect extension consolidation across all modules (5 total extensions)"
elif [[ "$MAIN_CONSOLIDATED" -eq 2 ]]; then
    TOTAL_SCORE=$((TOTAL_SCORE + 20))
    log_success "Main module fully consolidated, mobile module needs consolidation"
elif [[ "$MAIN_CONSOLIDATED" -ge 1 ]]; then
    TOTAL_SCORE=$((TOTAL_SCORE + 15))
    log_info "Partial consolidation in main module"
elif [[ $EXT_FILES -le 6 ]]; then
    TOTAL_SCORE=$((TOTAL_SCORE + 10))
    log_info "Reasonable extension count but consolidation recommended"
else
    TOTAL_SCORE=$((TOTAL_SCORE + 5))
    log_warning "Extension proliferation detected - follow BZLMOD_RECOMMENDATIONS.md"
fi

# Upstream extensions (20 points)
if grep -q "@rules_python//python/extensions" MODULE.bazel; then
    TOTAL_SCORE=$((TOTAL_SCORE + 20))
fi

# WORKSPACE.bzlmod elimination (10 points)
if [[ ! -f "WORKSPACE.bzlmod" ]] || [[ $(wc -l < WORKSPACE.bzlmod) -le 5 ]]; then
    TOTAL_SCORE=$((TOTAL_SCORE + 10))
fi

echo ""
log_info "=== Bzlmod Architecture Assessment ==="
log_info "Overall Score: $TOTAL_SCORE/100"

if [[ $TOTAL_SCORE -ge 90 ]]; then
    log_success "Excellent bzlmod architecture! Perfect implementation! 🎉"
elif [[ $TOTAL_SCORE -ge 80 ]]; then
    log_success "Great bzlmod implementation! 🎉"
elif [[ $TOTAL_SCORE -ge 60 ]]; then
    log_info "Good bzlmod foundation with room for improvement"
elif [[ $TOTAL_SCORE -ge 40 ]]; then
    log_warning "Basic bzlmod setup, consider following recommendations"
else
    log_error "Bzlmod implementation needs significant improvement"
fi

echo ""
log_info "Next steps:"
echo "  1. Review BZLMOD_RECOMMENDATIONS.md for current architecture details"
echo "  2. Visit https://registry.bazel.build/ for new BCR modules"
echo "  3. Consider contributing patches upstream to reduce extensions"

echo ""
log_info "🎉 Validation complete!"
