"""
Entry point for roller-installer package.

Allows running with: python -m roller_installer
"""

import sys
from .cli.commands import main


if __name__ == "__main__":
    sys.exit(main())