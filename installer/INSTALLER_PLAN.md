# ROLLER Installer Plan

## Overview
A cross-platform installer for ROLLER (Fatal Racing/Whiplash decompilation) that handles binary distribution, asset extraction, and configuration setup.

## Implementation Status

### ✅ Completed
- **Project Structure**: Full directory layout created with proper Python package structure
- **Package Setup**: Basic pyproject.toml configuration with project metadata
- **Directory Structure**: All planned directories created with .gitkeep files where needed:
  - `roller_installer/` (main package with `__init__.py`)
  - `roller_installer/core/` (with `__init__.py`)
  - `roller_installer/gui/` (with `__init__.py`) 
  - `roller_installer/tui/` (with `__init__.py`)
  - `roller_installer/cli/` (with `__init__.py`)
  - `roller_installer/utils/` (with `__init__.py`)
  - `scripts/` (with `.gitkeep`)
  - `assets/` (with `.gitkeep`)
  - `assets/templates/` (with `.gitkeep`)
  - `tests/` (with `__init__.py`)

### 🔄 In Progress
- Planning and design phase

### 📋 Todo
- Dependencies configuration (requirements.txt, pyproject.toml updates)
- Core installer logic implementation
- GUI interface development
- TUI interface development  
- CLI interface development
- Build system setup
- Testing framework

## Architecture

### Core Components
1. **GUI Interface** (tkinter) - Main user interface
2. **TUI Interface** (textual/rich) - Retro text-based installer
3. **CLI Interface** (argparse) - Command-line automation
4. **Core Installer Logic** - Shared business logic
5. **Native Binary Builder** (PyInstaller/Nuitka) - Single executable distribution

### Project Structure
```
installer/
├── pyproject.toml              # Project configuration
├── README.md                   # Installation and usage
├── requirements.txt            # Runtime dependencies
├── requirements-dev.txt        # Development dependencies
├── roller_installer/           # Main package
│   ├── __init__.py
│   ├── __main__.py            # Entry point (python -m roller_installer)
│   ├── core/                   # Core installer logic
│   │   ├── __init__.py
│   │   ├── installer.py       # Main installer class
│   │   ├── github_client.py   # GitHub API integration
│   │   ├── asset_extractor.py # Handle zip/iso extraction
│   │   ├── config_generator.py # Generate ROLLER configs
│   │   └── updater.py         # Update checker
│   ├── gui/                    # GUI interface
│   │   ├── __init__.py
│   │   ├── main_window.py     # Main tkinter window
│   │   ├── dialogs.py         # File/folder selection dialogs
│   │   └── progress.py        # Progress indicators
│   ├── tui/                    # Text-based interface
│   │   ├── __init__.py
│   │   ├── main_menu.py       # Retro TUI main menu
│   │   └── installer_tui.py   # TUI installer flow
│   ├── cli/                    # Command-line interface
│   │   ├── __init__.py
│   │   └── commands.py        # CLI argument parsing
│   └── utils/                  # Shared utilities
│       ├── __init__.py
│       ├── platform_utils.py  # OS-specific helpers
│       ├── file_utils.py      # File operations
│       └── validation.py      # Input validation
├── scripts/                    # Build and deployment
│   ├── build_native.py        # Build native binaries
│   ├── build_all.py          # Build for all platforms
│   └── test_installer.py      # Integration tests
├── assets/                     # Static assets
│   ├── icon.ico               # Application icon
│   ├── splash.png            # Splash screen
│   └── templates/            # Config file templates
│       ├── roller_config.ini
│       └── launcher.sh
└── tests/                      # Unit tests
    ├── __init__.py
    ├── test_installer.py
    ├── test_github_client.py
    └── test_asset_extractor.py
```

## Features

### Core Functionality
✅ **Binary Download**
- Download latest ROLLER binary from GitHub releases
- Support for multiple architectures (Windows x64, Linux x64, macOS arm64/x64)
- Verify checksums/signatures if provided
- Resume interrupted downloads

✅ **Asset Detection & Extraction**
- **Folder**: Direct FATDATA directory selection
- **ZIP**: Extract FATDATA from ZIP archive (handles nested structures)
- **ISO**: Mount and extract FATDATA from ISO images
  - **CD Audio Extraction**: Optional extraction of audio tracks for enhanced music experience
  - **MIDI vs CD Audio Choice**: User preference between original MIDI and CD audio tracks
- Validate presence of required files (FATAL.INI, etc.)
- Handle different game versions (Fatal Racing vs Whiplash)

✅ **Installation Management**
- **Default Install Paths**:
  - Windows: `C:\Games\ROLLER`
  - macOS: `~/Applications/ROLLER`
  - Linux: `~/.local/share/ROLLER` or `/opt/ROLLER`
- Create desktop shortcuts and start menu entries
- File association for .TRK track files
- Proper permissions setup

✅ **Configuration Generation**
- **Primary Config Files**:
  - `FATAL.INI` - Main game configuration (audio, video, controls, player names)
  - `config.ini` - Sound language configuration (Language=english, SoundCard settings)
- **Configuration Options**:
  - Audio settings (Engine/SFX/Speech/Music volume, MIDI vs CD audio)
  - Video settings (SVGA mode, screen size, graphical detail levels)
  - Control mapping (Player 1/2 keyboard controls, joystick calibration)
  - Player preferences (names, car selections, damage levels)
  - Network settings (modem configuration, multiplayer messages)

✅ **Update Management**
- Check for ROLLER updates on startup
- Background update checks
- Optional automatic updates
- Update notifications

✅ **Self-Updating Installer**
- **Installer Version Checking**: Check for newer installer releases on GitHub
- **Self-Update Command**: `roller-installer self-update` to update the installer itself
- **Automatic Self-Update**: Optional prompt to update installer before installing ROLLER
- **Rollback Support**: Keep previous installer version for emergency rollback
- **Update Verification**: Verify downloaded installer binaries with checksums
- **Cross-Platform**: Handle self-updates on Windows (.exe), macOS, and Linux

### User Interfaces

#### 1. GUI Interface (Primary)
**Main Window Features:**
- Welcome screen with ROLLER logo
- Step-by-step wizard flow
- Progress bars with detailed status
- Error handling with user-friendly messages
- Settings panel for advanced options

**Wizard Steps:**
1. **Welcome** - Introduction and system requirements check
2. **Source Selection** - Choose Fatal Racing installation source
3. **Binary Download** - Download ROLLER executable
4. **Install Location** - Choose installation directory
5. **Configuration** - Audio/video/input settings
6. **Installation** - Progress tracking
7. **Complete** - Launch option and shortcuts

#### 2. TUI Interface (Retro)
**Retro DOS-style installer:**
- Blue background with yellow text (classic installer aesthetic)
- ASCII art ROLLER logo
- Keyboard navigation
- Progress indicators using ASCII art
- Error dialogs in classic style

```
╔══════════════════════════════════════════════════════════════╗
║                        ROLLER v1.0                          ║
║                    Installation Program                      ║
╠══════════════════════════════════════════════════════════════╣
║                                                              ║
║  Welcome to the ROLLER installation program.                ║
║                                                              ║
║  This will install ROLLER (Fatal Racing decompilation)      ║
║  on your computer.                                           ║
║                                                              ║
║  [Enter] Continue    [Esc] Exit                             ║
║                                                              ║
╚══════════════════════════════════════════════════════════════╝
```

#### 3. CLI Interface (Automation)
**Command Examples:**
```bash
# Interactive installation
roller-installer

# Automated installation
roller-installer --source /path/to/fatdata --install-dir /opt/roller --auto-confirm

# Update check only
roller-installer --check-updates

# Extract assets only
roller-installer --extract-only --source game.iso --output ./extracted
```

### Technical Implementation

#### 1. Native Binary Building
**PyInstaller Configuration:**
- Single executable output
- Include all dependencies
- Custom icon and version info
- UPX compression for smaller size
- Code signing on Windows/macOS

**Build Targets:**
- `roller-installer-windows-x64.exe`
- `roller-installer-linux-x64`
- `roller-installer-macos-universal`

#### 2. Cross-Platform Considerations
**Windows:**
- Handle UAC elevation for system-wide installs
- Registry entries for uninstaller
- Windows Defender exclusions
- Proper file associations

**macOS:**
- Code signing and notarization
- Gatekeeper compatibility
- .app bundle creation
- DMG packaging

**Linux:**
- .desktop file creation
- AppImage packaging option
- Dependency detection
- Package manager integration hints

#### 3. Asset Extraction Logic
**ZIP Handling:**
```python
def extract_fatdata_from_zip(zip_path, output_dir):
    # Handle nested FATDATA directories
    # Support multiple archive formats (ZIP, 7Z, RAR)
    # Validate extracted files
    # Progress callbacks
```

**ISO Handling:**
```python
def extract_fatdata_from_iso(iso_path, output_dir):
    # Mount ISO (platform-specific)
    # Locate FATDATA directory
    # Copy files with progress tracking
    # Unmount cleanup
```

## Dependencies

### Runtime Dependencies
```toml
dependencies = [
    "requests>=2.31.0",        # GitHub API calls
    "tkinter",                 # GUI (built-in Python)
    "textual>=0.38.0",        # TUI interface
    "rich>=13.0.0",           # CLI formatting
    "py7zr>=0.20.0",          # 7Z archive support
    "rarfile>=4.0",           # RAR archive support
    "pycdlib>=1.13.0",        # ISO handling
    "platformdirs>=3.0.0",    # Cross-platform directories
    "appdirs>=1.4.4",         # Application directories
    "pyyaml>=6.0",            # Configuration files
]
```

### Development Dependencies
```toml
dev-dependencies = [
    "pytest>=7.0.0",
    "pytest-cov>=4.0.0",
    "pyinstaller>=5.13.0",
    "nuitka>=1.8.0",
    "black>=23.0.0",
    "flake8>=6.0.0",
    "mypy>=1.5.0",
]
```

## Missing Features You Should Consider

### 1. **Uninstaller**
- Generate uninstaller during installation
- Remove all installed files and shortcuts
- Registry cleanup on Windows
- Config backup option

### 2. **Multiple Game Version Support**
- Detect Fatal Racing vs Whiplash vs other versions
- Version-specific configuration
- Compatibility warnings

### 3. **Mod Support**
- Optional mod installation framework
- Mod manager integration
- Community mod repository

### 4. **Backup & Restore**
- Backup existing saves/configs before update
- Restore previous installation
- Migration from other ROLLER installations

### 5. **Diagnostic Tools**
- System compatibility check
- Graphics driver detection
- Performance benchmarking
- Log file analysis

### 6. **Localization**
- Multi-language support
- Translated installer interface
- Region-specific defaults

### 7. **Network Features**
- Mirror server support for downloads
- Torrent-based distribution
- Offline installer creation

## Build System

### Development Setup
```bash
cd installer
pip install -e .
pip install -r requirements-dev.txt

# Run in development mode
python -m roller_installer --gui

# Run tests
pytest
```

### Native Binary Building
```bash
# Build for current platform
python scripts/build_native.py

# Build for all platforms (requires Docker/VM)
python scripts/build_all.py

# Test built executable
python scripts/test_installer.py
```

## Distribution Strategy

### 1. **GitHub Releases**
- Upload native binaries to GitHub releases
- Automated builds via GitHub Actions
- Release notes with changelog

### 2. **Package Managers**
```bash
# Python Package Index
pip install roller-installer

# Windows Package Manager
winget install ROLLER.Installer

# Homebrew (macOS)
brew install --cask roller-installer

# Linux Package Managers
sudo apt install roller-installer    # Future: PPA
flatpak install roller-installer     # Future: Flathub
```

### 3. **Web Distribution**
- Lightweight web installer
- Downloads full installer on demand
- Always gets latest version

## Security Considerations

1. **Code Signing**: Sign executables on Windows/macOS
2. **Checksum Verification**: Verify downloaded binaries
3. **Secure Downloads**: HTTPS only, certificate pinning
4. **Sandbox Permissions**: Minimal required permissions
5. **User Consent**: Clear permission requests

## This plan covers everything you mentioned plus several important additions. The key missing pieces I identified are the uninstaller, multi-version support, and diagnostic tools. Would you like me to start implementing any specific component?