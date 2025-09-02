# ROLLER Installer

Cross-platform installer for ROLLER (Fatal Racing/Whiplash decompilation) that handles binary distribution, asset extraction, and configuration setup.

## Current Features

✅ **Modern CLI Interface**
- Built with Typer for professional command-line experience
- Rich terminal output with progress bars and colored text
- Three planned interfaces: CLI (✅), GUI (📋), TUI (📋)

✅ **Native Binary Building**
- Single-file executables with PyInstaller
- Cross-platform support (Windows, macOS, Linux)
- ~14MB self-contained binaries

✅ **Configuration Management**
- Generates `FATAL.INI` with game settings (audio, video, controls)
- Creates `config.ini` for language and sound card setup
- Template-based configuration with sensible defaults

## Available Commands

```bash
# Install latest ROLLER version
roller-installer install

# Install specific version
roller-installer install --version v1.2.3

# Custom installation directory
roller-installer install --install-dir /opt/roller

# Check for updates
roller-installer check-updates

# List available releases
roller-installer list-releases

# Show help
roller-installer --help
```

## Development Setup

### Prerequisites
- Python 3.9+ (compatible with PyInstaller)
- Poetry 2
- mise (for task running)

### Install Dependencies
```bash
poetry install
```

### Development Commands

```bash
# Run from source
mise run installer -- --help
mise run installer -- install --version v1.0.0

# Build native binary
mise run build-installer

# Test built binary
./dist/roller-installer --help
```

### Project Structure
```
installer/
├── pyproject.toml              # Project configuration
├── main.py                     # Entry point for binary building
├── roller_installer/           # Main package
│   ├── __main__.py            # Entry point (python -m roller_installer)
│   ├── cli/                   # CLI interface
│   │   └── commands.py        # Typer CLI implementation
│   ├── core/                  # Core installer logic (📋 TODO)
│   ├── gui/                   # GUI interface (📋 TODO)
│   ├── tui/                   # TUI interface (📋 TODO)
│   └── utils/                 # Shared utilities (📋 TODO)
├── assets/
│   └── templates/             # Config file templates
│       ├── FATAL.INI         # Game configuration template
│       └── config.ini        # Sound/language template
└── tests/                     # Test suite (📋 TODO)
```

## Planned Features

📋 **GitHub Integration**
- Download ROLLER binaries from GitHub releases
- Update checking and notifications
- Version management

📋 **Asset Management**
- Extract FATDATA from folders, ZIP files, or ISO images
- CD audio extraction from ISOs
- Game file validation

📋 **Installation Management**
- Cross-platform installation paths
- Desktop shortcuts and file associations
- Uninstaller generation

📋 **Additional Interfaces**
- GUI installer with step-by-step wizard
- Retro TUI installer matching Fatal Racing aesthetics

## Building Distributions

### Single Platform
```bash
# Build for current platform
mise run build-installer
# Output: dist/roller-installer (or .exe on Windows)
```

### Cross-Platform
- Windows: Build on Windows or use Docker/CI
- macOS: Build on macOS (requires code signing)
- Linux: Build on Linux (works in containers)

## Installation Flow

1. **Download** - Get latest ROLLER binary from GitHub
2. **Asset Detection** - Locate Fatal Racing/Whiplash FATDATA
3. **Installation** - Copy binary and assets to target directory
4. **Configuration** - Generate FATAL.INI and config.ini
5. **Integration** - Create shortcuts, file associations
6. **Verification** - Test installation and cleanup

## Contributing

This installer follows the roadmap in `INSTALLER_PHASE_1.md`. Current implementation status:

- ✅ **CLI Framework** - Typer-based CLI with all commands
- ✅ **Build System** - PyInstaller native binary generation
- ✅ **Project Structure** - Complete package layout
- 📋 **GitHub Client** - For downloading releases
- 📋 **Installation Logic** - Binary and config deployment
- 📋 **Asset Extraction** - ZIP/ISO handling

See `INSTALLER_PLAN.md` for the complete feature roadmap.