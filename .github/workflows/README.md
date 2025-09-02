# ROLLER Installer CI/CD Workflows

This directory contains GitHub Actions workflows for the ROLLER installer project.

## Workflows

### 🏗️ Build Installer (`build-installer.yml`)
**Triggers:** Push/PR to installer branch, releases, manual dispatch

**Purpose:** Builds native installer binaries for multiple platforms using PyInstaller

**Platforms:**
- Linux x64 (Ubuntu 22.04)
- macOS Intel (macOS 13)
- macOS ARM64 (macOS 14) 
- Windows x64 (Windows Server 2022)

**Features:**
- ✅ Cross-platform matrix builds
- ✅ Poetry dependency caching
- ✅ mise action integration
- ✅ Artifact upload with retention
- ✅ Release asset automation
- ✅ Security scanning
- ✅ Build status verification

### 🧪 Test Installer (`test-installer.yml`)
**Triggers:** Push/PR to installer branch, manual dispatch

**Purpose:** Comprehensive testing of installer code and build process

**Test Coverage:**
- Code linting and formatting (Black, Flake8, isort)
- Type checking (mypy)
- Build process validation
- Binary functionality testing
- Python version compatibility (3.9-3.12)
- Integration testing in clean environment

### 🔧 Installer Maintenance (`installer-maintenance.yml`)
**Triggers:** Weekly schedule (Sundays 2 AM UTC), manual dispatch

**Purpose:** Automated maintenance and dependency management

**Features:**
- Dependency update checking
- Security vulnerability scanning (Safety)
- Build configuration validation
- Performance optimization recommendations

## Badge Integration

Add these badges to your README.md:

```markdown
[![Build Installer](https://github.com/YOUR_USERNAME/roller/actions/workflows/build-installer.yml/badge.svg)](https://github.com/YOUR_USERNAME/roller/actions/workflows/build-installer.yml)
[![Test Installer](https://github.com/YOUR_USERNAME/roller/actions/workflows/test-installer.yml/badge.svg)](https://github.com/YOUR_USERNAME/roller/actions/workflows/test-installer.yml)
[![Maintenance](https://github.com/YOUR_USERNAME/roller/actions/workflows/installer-maintenance.yml/badge.svg)](https://github.com/YOUR_USERNAME/roller/actions/workflows/installer-maintenance.yml)
```

## Workflow Dependencies

- **mise**: Task runner and tool version management
- **Poetry 2**: Python dependency management  
- **PyInstaller**: Native binary compilation

## Security Considerations

- All workflows use pinned action versions for security
- Secrets are properly scoped and not exposed in logs
- Binary scanning for embedded secrets
- Automated security vulnerability checking
- Minimal permissions following principle of least privilege

## Performance Optimizations

- Poetry dependency caching across runs
- mise tool caching
- Conditional triggering on relevant path changes
- Parallel matrix builds
- Optimized artifact compression

## Troubleshooting

### Common Issues

1. **Build failures on specific platforms**
   - Check PyInstaller compatibility with dependencies
   - Verify Python version compatibility
   - Review platform-specific binary requirements

2. **Cache invalidation**
   - Manually clear workflow caches if dependencies change
   - Check cache key patterns match your setup

3. **Artifact upload failures**
   - Verify binary paths match expected locations
   - Check artifact naming consistency across platforms

### Local Testing

Test workflows locally using [act](https://github.com/nektos/act):

```bash
# Test the build workflow
act -W .github/workflows/build-installer.yml

# Test with specific event
act pull_request -W .github/workflows/test-installer.yml
```