#!/bin/bash
# Bazel 8 Module Tidying Script
#
# This script demonstrates Bazel 8's `bazel mod tidy` feature which automatically
# maintains MODULE.bazel files by:
# - Formatting the file
# - Updating use_repo() calls to match what extensions actually provide
# - Removing unused repository references
# - Adding missing repository references
#
# This eliminates manual maintenance of 100+ repository declarations!

set -e

echo "═══════════════════════════════════════════════════════════"
echo "Bazel 8 Module Tidy - Automated MODULE.bazel Maintenance"
echo "═══════════════════════════════════════════════════════════"
echo ""

echo "📋 Before: Manual maintenance of use_repo() declarations"
echo "   - 100+ repository names to keep in sync"
echo "   - Easy to miss additions/removals from extensions"
echo "   - Manual formatting and organization"
echo ""

echo "✨ Bazel 8 Feature: bazel mod tidy"
echo ""
echo "Running: bazel mod tidy --enable_bzlmod"
echo ""

if bazel mod tidy --enable_bzlmod; then
    echo ""
    echo "✅ Success! MODULE.bazel has been automatically tidied:"
    echo "   - All use_repo() calls updated to match extensions"
    echo "   - File formatted consistently"  
    echo "   - Unused repos removed"
    echo "   - Missing repos added"
    echo ""
    echo "📊 Check the diff to see changes:"
    echo "   git diff MODULE.bazel mobile/MODULE.bazel"
    echo ""
    echo "This automation reduces maintenance burden and prevents errors!"
else
    echo ""
    echo "⚠️  Tidy encountered issues - check output above"
    exit 1
fi

echo ""
echo "═══════════════════════════════════════════════════════════"
echo "Bazel 8 delivers on the promise of reduced boilerplate! 🎉"
echo "═══════════════════════════════════════════════════════════"
