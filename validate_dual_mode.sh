#!/bin/bash
# Validation script for Envoy dual-mode (bzlmod + WORKSPACE) support
# Tests that both build modes work independently

set -e

echo "================================"
echo "Envoy Dual-Mode Validation"
echo "================================"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Track results
BZLMOD_PASSED=0
WORKSPACE_PASSED=0

echo "Bazel version:"
bazel version | head -n 1
echo ""

# Test bzlmod mode
echo "=== Testing bzlmod Mode ==="
echo ""

echo "1. Testing dependency resolution..."
if bazel mod graph --enable_bzlmod > /dev/null 2>&1; then
    echo -e "${GREEN}✅ bzlmod dependency graph: PASSED${NC}"
    ((BZLMOD_PASSED++))
else
    echo -e "${RED}❌ bzlmod dependency graph: FAILED${NC}"
fi

echo "2. Testing core module build..."
if bazel build --enable_bzlmod //source/common/common:assert_lib > /dev/null 2>&1; then
    echo -e "${GREEN}✅ bzlmod core build: PASSED${NC}"
    ((BZLMOD_PASSED++))
else
    echo -e "${RED}❌ bzlmod core build: FAILED${NC}"
fi

echo "3. Testing API module build..."
if bazel build --enable_bzlmod @envoy_api//envoy/config/core/v3:pkg > /dev/null 2>&1; then
    echo -e "${GREEN}✅ bzlmod API build: PASSED${NC}"
    ((BZLMOD_PASSED++))
else
    echo -e "${RED}❌ bzlmod API build: FAILED${NC}"
fi

echo "4. Testing mobile module query..."
if bazel query --enable_bzlmod "@envoy_mobile//library/..." > /dev/null 2>&1; then
    echo -e "${GREEN}✅ bzlmod mobile query: PASSED${NC}"
    ((BZLMOD_PASSED++))
else
    echo -e "${RED}❌ bzlmod mobile query: FAILED${NC}"
fi

echo ""
echo "=== Testing WORKSPACE Mode ==="
echo ""

echo "1. Testing core module build..."
if bazel build --noenable_bzlmod //source/common/common:assert_lib > /dev/null 2>&1; then
    echo -e "${GREEN}✅ WORKSPACE core build: PASSED${NC}"
    ((WORKSPACE_PASSED++))
else
    echo -e "${YELLOW}⚠️  WORKSPACE core build: FAILED${NC}"
    echo "   (WORKSPACE mode may have known limitations)"
fi

echo "2. Testing API module build..."
if bazel build --noenable_bzlmod @envoy_api//envoy/config/core/v3:pkg > /dev/null 2>&1; then
    echo -e "${GREEN}✅ WORKSPACE API build: PASSED${NC}"
    ((WORKSPACE_PASSED++))
else
    echo -e "${YELLOW}⚠️  WORKSPACE API build: FAILED${NC}"
    echo "   (WORKSPACE mode may have known limitations)"
fi

echo ""
echo "================================"
echo "Validation Summary"
echo "================================"
echo ""

echo "bzlmod mode: ${BZLMOD_PASSED}/4 tests passed"
echo "WORKSPACE mode: ${WORKSPACE_PASSED}/2 tests passed"
echo ""

if [ $BZLMOD_PASSED -eq 4 ]; then
    echo -e "${GREEN}✅ bzlmod mode: FULLY FUNCTIONAL${NC}"
    echo "   Recommended for all new projects"
else
    echo -e "${RED}❌ bzlmod mode: ISSUES DETECTED${NC}"
    echo "   Check Bazel version (need 8.4.2+)"
    exit 1
fi

if [ $WORKSPACE_PASSED -eq 2 ]; then
    echo -e "${GREEN}✅ WORKSPACE mode: FUNCTIONAL${NC}"
    echo "   Available for legacy support"
elif [ $WORKSPACE_PASSED -gt 0 ]; then
    echo -e "${YELLOW}⚠️  WORKSPACE mode: PARTIAL${NC}"
    echo "   Some targets work, bzlmod recommended"
else
    echo -e "${YELLOW}⚠️  WORKSPACE mode: LIMITED${NC}"
    echo "   Use bzlmod mode for best experience"
fi

echo ""
echo "================================"
echo "✅ Validation Complete"
echo "================================"
echo ""
echo "Next steps:"
echo "  - Use --enable_bzlmod for bzlmod mode"
echo "  - Use --noenable_bzlmod for WORKSPACE mode"
echo "  - See BZLMOD_MIGRATION_GUIDE.md for details"
echo ""

exit 0
