# ROLLER Installer - Phase 1

## Overview
Phase 1 focuses on implementing the core CLI functionality for downloading and installing ROLLER binaries from GitHub releases. This provides the essential foundation that can be extended with GUI and TUI interfaces later.

## Goals
- ✅ **Functional CLI installer** that can download and install ROLLER
- ✅ **GitHub integration** for release management and updates  
- ✅ **Cross-platform binary installation** with proper system integration
- ✅ **Update checking** and version management
- ✅ **Robust error handling** and user feedback

## Phase 1 Tasks

### Task 1: CLI Framework with Argument Parsing
**Objective**: Create a robust command-line interface using a professional argument parsing library.

**Implementation Details**:
- **Library**: Use `typer` (recommended) or `click` for modern CLI development
  - `typer`: **PREFERRED** - Type-hint friendly, built on click, modern Python 3.7+, cleaner code
  - `click`: Battle-tested, widely adopted, excellent documentation, more customizable
- **Alternative**: `argparse` (stdlib) for minimal dependencies

**✅ Perplexity Research Confirms (2025)**:
- **Typer** is the current best choice for new CLI projects - type-safe, modern, excellent documentation
- **Click** remains excellent for projects requiring advanced customization
- **PyGithub** is the most actively maintained and recommended GitHub API client

**Commands to Implement**:
```bash
# Install latest version (default behavior)
roller-installer install

# Install specific version
roller-installer install --version v1.2.3

# Install from specific GitHub release
roller-installer install --release latest
roller-installer install --release v1.2.3

# Check for updates
roller-installer check-updates

# Show version information
roller-installer --version

# Verbose output
roller-installer install --verbose

# Specify installation directory
roller-installer install --install-dir /path/to/install

# List available releases
roller-installer list-releases

# Self-update the installer
roller-installer self-update

# Check installer version
roller-installer self-update --check-only

# Show help
roller-installer --help
```

**CLI Structure**:
```python
# roller_installer/cli/commands.py
import click
from rich.console import Console
from rich.progress import Progress

@click.group()
@click.version_option(version="0.0.1", prog_name="roller-installer")
@click.option('--verbose', '-v', is_flag=True, help='Enable verbose output')
@click.pass_context
def cli(ctx, verbose):
    """ROLLER Installer - Install and manage ROLLER (Fatal Racing decompilation)"""
    ctx.ensure_object(dict)
    ctx.obj['verbose'] = verbose

@cli.command()
@click.option('--version', help='Specific version to install')
@click.option('--install-dir', help='Installation directory')
@click.option('--force', is_flag=True, help='Force reinstall')
@click.pass_context
def install(ctx, version, install_dir, force):
    """Install ROLLER binary"""
    pass

@cli.command()
@click.pass_context  
def check_updates(ctx):
    """Check for ROLLER updates"""
    pass

@cli.command()
@click.option('--check-only', is_flag=True, help='Only check for updates, do not install')
@click.pass_context
def self_update(ctx, check_only):
    """Update the installer itself"""
    pass
```

**Dependencies**:
```toml
dependencies = [
    "typer>=0.9.0",          # Modern CLI framework (preferred)
    "rich>=13.0.0",          # Beautiful CLI output (auto-included with typer)
    # Alternative: "click>=8.1.0" if preferring click over typer
]
```

### Task 2: GitHub Client Integration
**Objective**: Implement GitHub API integration for release management and updates.

**Implementation Details**:
- **Library**: Use `PyGithub` for GitHub API interaction (**PREFERRED**)
  - `PyGithub`: Most actively maintained, comprehensive GitHub API wrapper, excellent type hints
  - `github3.py`: Less maintained as of 2025, not recommended for new projects
- **Features**:
  - List releases (latest, all, specific)
  - Download release assets (binaries)
  - Check for updates against current version
  - Handle API rate limiting
  - Support for GitHub tokens (for higher rate limits)

**GitHub Client Structure**:
```python
# roller_installer/core/github_client.py
from github import Github
from typing import Optional, List, Dict
import requests
from pathlib import Path

class RollerGitHubClient:
    def __init__(self, token: Optional[str] = None):
        """Initialize GitHub client with optional token for higher rate limits"""
        self.github = Github(token) if token else Github()
        self.repo = self.github.get_repo("OpenRoller/ROLLER")  # Adjust repo name
    
    def get_latest_release(self) -> Dict:
        """Get the latest release information"""
        
    def get_release_by_tag(self, tag: str) -> Dict:
        """Get specific release by tag name"""
        
    def list_releases(self, limit: int = 10) -> List[Dict]:
        """List recent releases"""
        
    def download_asset(self, asset_url: str, destination: Path, 
                      progress_callback=None) -> bool:
        """Download release asset with progress tracking"""
        
    def check_for_updates(self, current_version: str) -> Optional[Dict]:
        """Check if updates are available"""
        
    def get_platform_asset(self, release_data: Dict, platform: str) -> Optional[Dict]:
        """Get the appropriate asset for current platform"""
        
    def check_installer_updates(self, current_version: str) -> Optional[Dict]:
        """Check if installer itself has updates available"""
        
    def get_installer_asset(self, release_data: Dict, platform: str) -> Optional[Dict]:
        """Get installer binary asset for platform (roller-installer-windows-x64.exe, etc.)"""
```

**Platform Detection**:
```python
# roller_installer/utils/platform_utils.py
import platform
import sys

def get_platform_identifier() -> str:
    """Return platform identifier for asset matching"""
    system = platform.system().lower()
    machine = platform.machine().lower()
    
    # Map to GitHub release asset naming convention
    platform_map = {
        ('windows', 'x86_64'): 'windows-x64',
        ('windows', 'amd64'): 'windows-x64',
        ('linux', 'x86_64'): 'linux-x64',
        ('darwin', 'x86_64'): 'macos-x64',
        ('darwin', 'arm64'): 'macos-arm64',
    }
    
    return platform_map.get((system, machine), f"{system}-{machine}")

def get_executable_extension() -> str:
    """Return appropriate executable extension for platform"""
    return '.exe' if platform.system() == 'Windows' else ''
```

**Dependencies**:
```toml
dependencies = [
    "PyGithub>=2.1.0",
    "requests>=2.31.0",
]
```

### Task 3: Binary Download and Installation
**Objective**: Download ROLLER binaries from GitHub releases and install them properly on the target system, including configuration file generation.

**Implementation Details**:

**Download Manager**:
```python
# roller_installer/core/downloader.py
import requests
from pathlib import Path
from typing import Callable, Optional
from rich.progress import Progress, TaskID

class RollerDownloader:
    def __init__(self):
        self.session = requests.Session()
        self.session.headers.update({
            'User-Agent': 'roller-installer/0.0.1'
        })
    
    def download_file(self, url: str, destination: Path, 
                     progress_callback: Optional[Callable] = None) -> bool:
        """Download file with progress tracking and resumption support"""
        
    def verify_download(self, file_path: Path, expected_hash: Optional[str] = None) -> bool:
        """Verify downloaded file integrity"""
        
    def extract_archive(self, archive_path: Path, destination: Path) -> bool:
        """Extract downloaded archive if needed"""
```

**Installation Manager**:
```python
# roller_installer/core/installer.py
from pathlib import Path
from typing import Dict, Optional
import shutil
import stat

class RollerInstaller:
    def __init__(self, install_dir: Optional[Path] = None):
        self.install_dir = install_dir or self.get_default_install_dir()
    
    def get_default_install_dir(self) -> Path:
        """Get platform-appropriate default installation directory"""
        
    def install_binary(self, binary_path: Path, version: str) -> bool:
        """Install ROLLER binary to target directory"""
        
    def create_shortcuts(self) -> bool:
        """Create desktop shortcuts and start menu entries"""
        
    def setup_file_associations(self) -> bool:
        """Setup file associations for .TRK files"""
        
    def generate_config_files(self) -> bool:
        """Generate ROLLER configuration files with sensible defaults"""
        
    def create_uninstaller(self) -> bool:
        """Create uninstaller script/entry"""
        
    def verify_installation(self) -> bool:
        """Verify installation completed successfully"""
        
    def self_update(self, new_binary_path: Path) -> bool:
        """Update the installer itself with a new version"""
        
    def backup_current_installer(self) -> Path:
        """Create backup of current installer for rollback"""
```

**Installation Flow**:
1. **Detect Platform** - Identify OS and architecture
2. **Select Asset** - Choose appropriate binary from GitHub release  
3. **Download Binary** - Download with progress indication and verification
4. **Create Install Directory** - Set up installation location with proper permissions
5. **Install Binary** - Copy/extract to final location
6. **Generate Config Files** - Create FATAL.INI and config.ini with defaults
7. **Set Permissions** - Make executable on Unix-like systems
8. **Create Integration** - Desktop shortcuts, file associations, PATH updates
9. **Verify Installation** - Test that ROLLER can be launched
10. **Cleanup** - Remove temporary download files

**Config File Generation**:
- **`FATAL.INI`** - Main game configuration with audio/video/control defaults
- **`config.ini`** - Sound language configuration (Language=english, SoundCard=1)
- Templates stored in `assets/templates/` and customized during installation

**Default Installation Paths**:
- **Windows**: `%LOCALAPPDATA%\Programs\ROLLER` or `C:\Program Files\ROLLER`
- **macOS**: `~/Applications/ROLLER` or `/Applications/ROLLER`
- **Linux**: `~/.local/share/ROLLER` or `/opt/ROLLER`

**Self-Update Flow**:
1. **Check Current Version** - Get version from executable metadata or embedded version
2. **Query GitHub API** - Check for newer installer releases
3. **Download New Binary** - Download platform-specific installer binary
4. **Verify Download** - Check checksums and signatures
5. **Backup Current** - Save current installer as `.bak` file
6. **Replace Binary** - Atomic replacement of current executable
7. **Cleanup** - Remove backup after successful verification
8. **Restart Prompt** - Option to restart with new version

**Self-Update Challenges & Solutions**:
- **Running Binary Replacement**: Use platform-specific techniques (Windows: batch scripts, Unix: exec)
- **Permissions**: Handle elevated permissions for system-wide installations
- **Rollback**: Keep previous version for emergency rollback
- **Cross-Platform**: Different approaches for Windows (.exe) vs Unix systems

**Dependencies**:
```toml
dependencies = [
    "requests>=2.31.0",
    "platformdirs>=3.0.0",  # Cross-platform directory detection
    "psutil>=5.9.0",       # Process management for self-updates
]
```

## Phase 1 Integration

### Main CLI Entry Point
```python
# roller_installer/cli/commands.py (expanded)

@cli.command()
@click.option('--version', help='Specific version to install (e.g., v1.2.3)')
@click.option('--install-dir', type=click.Path(), help='Custom installation directory')
@click.option('--force', is_flag=True, help='Force reinstall even if already installed')
@click.option('--no-shortcuts', is_flag=True, help='Skip creating desktop shortcuts')
@click.pass_context
def install(ctx, version, install_dir, force, no_shortcuts):
    """Install ROLLER binary from GitHub releases"""
    console = Console()
    
    try:
        # Initialize components
        github_client = RollerGitHubClient()
        installer = RollerInstaller(Path(install_dir) if install_dir else None)
        downloader = RollerDownloader()
        
        # Get release information
        if version:
            release = github_client.get_release_by_tag(version)
        else:
            release = github_client.get_latest_release()
            
        console.print(f"Installing ROLLER {release['tag_name']}...")
        
        # Download and install
        with Progress() as progress:
            task = progress.add_task("Downloading...", total=100)
            
            # Download binary
            asset = github_client.get_platform_asset(release, get_platform_identifier())
            download_path = downloader.download_file(
                asset['browser_download_url'],
                Path.cwd() / asset['name'],
                lambda p: progress.update(task, completed=p)
            )
            
            # Install
            progress.update(task, description="Installing...")
            installer.install_binary(download_path, release['tag_name'])
            
            if not no_shortcuts:
                installer.create_shortcuts()
                
            progress.update(task, completed=100)
            
        console.print(f"✅ ROLLER {release['tag_name']} installed successfully!")
        
    except Exception as e:
        console.print(f"❌ Installation failed: {e}", style="red")
        raise click.Abort()
```

## Testing Strategy

### Unit Tests
```python
# tests/test_github_client.py
import pytest
from unittest.mock import Mock, patch
from roller_installer.core.github_client import RollerGitHubClient

class TestRollerGitHubClient:
    def test_get_latest_release(self):
        """Test fetching latest release"""
        
    def test_platform_asset_selection(self):
        """Test selecting correct asset for platform"""
        
    def test_rate_limit_handling(self):
        """Test API rate limit handling"""
```

### Integration Tests  
```python
# tests/test_installation.py
import pytest
import tempfile
from pathlib import Path
from roller_installer.core.installer import RollerInstaller

class TestRollerInstaller:
    def test_binary_installation(self):
        """Test binary installation process"""
        
    def test_permissions_setting(self):
        """Test executable permissions are set correctly"""
        
    def test_shortcut_creation(self):
        """Test desktop shortcut creation"""
```

## Success Criteria

### Phase 1 Complete When:
1. ✅ **CLI Works**: `roller-installer install` successfully downloads and installs ROLLER
2. ✅ **GitHub Integration**: Can list releases, download assets, check for updates
3. ✅ **Cross-Platform**: Works on Windows, macOS, and Linux
4. ✅ **Error Handling**: Graceful failure with helpful error messages
5. ✅ **Installation Verification**: Installed ROLLER binary is functional
6. ✅ **User Experience**: Professional CLI output with progress indication

### Example Usage Flow
```bash
# Check what's available
$ roller-installer list-releases
Available ROLLER releases:
  v1.2.3 (latest) - Released 2024-09-01
  v1.2.2 - Released 2024-08-15  
  v1.2.1 - Released 2024-08-01

# Install latest version
$ roller-installer install
Installing ROLLER v1.2.3...
Downloading roller-windows-x64.exe  ██████████████████████████████ 100%
Installing to C:\Users\user\AppData\Local\Programs\ROLLER...
Creating desktop shortcuts...
✅ ROLLER v1.2.3 installed successfully!

# Check for updates later
$ roller-installer check-updates
✅ ROLLER is up to date (v1.2.3)

# Force reinstall specific version
$ roller-installer install --version v1.2.2 --force
Installing ROLLER v1.2.2...
...
```

## Dependencies Summary
```toml
# Add to pyproject.toml
dependencies = [
    "click>=8.1.0",           # CLI framework
    "rich>=13.0.0",           # Beautiful terminal output
    "PyGithub>=2.1.0",        # GitHub API integration
    "requests>=2.31.0",       # HTTP client
    "platformdirs>=3.0.0",    # Cross-platform directories
]
```

This phase provides a solid foundation that can be extended with GUI and TUI interfaces while ensuring the core functionality is robust and reliable.