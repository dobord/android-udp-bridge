#!/bin/bash

# Script for local testing of GitHub Actions workflows
# Requires act installed: https://github.com/nektos/act

set -e

# Output colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored text
print_color() {
    echo -e "${1}${2}${NC}"
}

print_header() {
    echo "================================"
    print_color $BLUE "$1"
    echo "================================"
}

# Check presence of act
if ! command -v act &> /dev/null; then
    print_color $RED "❌ act is not installed!"
    echo "Install act: https://github.com/nektos/act#installation"
    echo "Example: brew install act"
    exit 1
fi

# Check presence of Docker
if ! command -v docker &> /dev/null; then
    print_color $RED "❌ Docker is not installed!"
    echo "Install Docker to run act"
    exit 1
fi

print_header "🚀 Local testing of GitHub Actions"

echo "Available commands:"
echo "1. test-pr       - Test PR check workflow"
echo "2. test-build    - Test build workflow"
echo "3. test-release  - Test release workflow (with fake tag)"
echo "4. list          - Show all available workflows"
echo "5. validate      - Validate syntax of all workflows"

if [ $# -eq 0 ]; then
    echo ""
    print_color $YELLOW "Usage: $0 [command]"
    exit 1
fi

case $1 in
    "test-pr")
    print_header "🔍 Testing PR Check"
        act pull_request -W .github/workflows/pr-check.yml \
            --container-architecture linux/amd64 \
            --artifact-server-path /tmp/artifacts \
            -v
        ;;
        
    "test-build")
    print_header "🏗️ Testing Build and Release"
        act push -W .github/workflows/build-and-release.yml \
            --container-architecture linux/amd64 \
            --artifact-server-path /tmp/artifacts \
            -v
        ;;
        
    "test-release")
    print_header "📦 Testing Release"
    # Create temporary tag for testing
        git tag -f v99.99.99-test 2>/dev/null || true
        
        act push -W .github/workflows/release.yml \
            --container-architecture linux/amd64 \
            --artifact-server-path /tmp/artifacts \
            -e .github/test-events/tag-push.json \
            -v
            
    # Delete test tag
        git tag -d v99.99.99-test 2>/dev/null || true
        ;;
        
    "list")
    print_header "📋 Available Workflows"
        act -l
        ;;
        
    "validate")
    print_header "✅ Workflows Validation"
        
    # Check YAML syntax
        for file in .github/workflows/*.yml; do
            if [ -f "$file" ]; then
                echo "Checking $file..."
                
                # Simple YAML syntax validation
                if python3 -c "import yaml; yaml.safe_load(open('$file'))" 2>/dev/null; then
                    print_color $GREEN "✅ $file - OK"
                else
                    print_color $RED "❌ $file - SYNTAX ERROR"
                fi
            fi
        done
        
        # Validate with act
        echo ""
        echo "Validating with act..."
        if act -l >/dev/null 2>&1; then
            print_color $GREEN "✅ All workflows valid"
        else
            print_color $RED "❌ Errors found in workflows"
        fi
        ;;
        
    "setup")
        print_header "⚙️ Environment setup for testing"
        
        # Create directories for test events
        mkdir -p .github/test-events
        
        # Create tag push test event file
        cat > .github/test-events/tag-push.json << 'EOF'
{
  "ref": "refs/tags/v99.99.99-test",
  "ref_name": "v99.99.99-test",
  "repository": {
    "name": "android-udp-bridge",
    "full_name": "dobord/android-udp-bridge"
  }
}
EOF

    # Create .actrc for default settings
        cat > .actrc << 'EOF'
--container-architecture linux/amd64
--artifact-server-path /tmp/artifacts
--env-file .github/.env.test
EOF

    # Create test env file
        cat > .github/.env.test << 'EOF'
ANDROID_HOME=/opt/android-sdk
ANDROID_ABI=arm64-v8a
GITHUB_TOKEN=fake-token-for-testing
EOF

    print_color $GREEN "✅ Setup complete!"
    echo "Files created:"
        echo "  - .github/test-events/tag-push.json"
        echo "  - .actrc"
        echo "  - .github/.env.test"
        ;;
        
    "clean")
    print_header "🧹 Cleaning test files"
        
        rm -rf /tmp/artifacts
        rm -f .github/test-events/tag-push.json
        rm -f .actrc
        rm -f .github/.env.test
        
    print_color $GREEN "✅ Cleanup finished"
        ;;
        
    *)
    print_color $RED "❌ Unknown command: $1"
        echo ""
    echo "Available commands: test-pr, test-build, test-release, list, validate, setup, clean"
        exit 1
        ;;
esac

print_color $GREEN "✅ Command '$1' completed"
