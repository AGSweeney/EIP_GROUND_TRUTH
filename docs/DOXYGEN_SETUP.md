# Doxygen Documentation Setup

This document explains how to generate API documentation for the ESP32-P4 EtherNet/IP Device project using Doxygen.

## Quick Start

### Option 1: Use the Provided Scripts (Recommended)

**Windows PowerShell:**
```powershell
.\scripts\generate_doxygen_docs.ps1
```

**Windows Command Prompt:**
```cmd
scripts\generate_doxygen_docs.bat
```

### Option 2: Manual Generation

1. Install Doxygen (see Installation section below)
2. Run from project root:
   ```bash
   doxygen Doxyfile
   ```
3. Open `docs/doxygen/html/index.html` in your browser

## Installation

### Windows

**Method 1: Download Installer (Recommended)**
1. Download from: https://www.doxygen.nl/download.html
2. Run the installer
3. Restart your terminal/IDE

**Method 2: Using winget**
```powershell
winget install Doxygen.Doxygen
```

**Method 3: Using Chocolatey**
```powershell
choco install doxygen
```

### Verify Installation

After installation, verify Doxygen is available:
```bash
doxygen --version
```

You should see a version number (e.g., `1.12.0`).

## Configuration

The project includes a pre-configured `Doxyfile` with the following settings:

- **Input**: `components` and `main` directories
- **Output**: `docs/doxygen/html/`
- **Recursive**: Enabled (scans subdirectories)
- **Excluded**: Third-party libraries (opener, lwip, esp_netif)
- **Language**: Optimized for C
- **HTML Output**: Enabled with search functionality

### Customizing Configuration

To modify the configuration:

1. Edit `Doxyfile` in the project root
2. Key settings:
   - `INPUT`: Source directories to document
   - `EXCLUDE_PATTERNS`: Patterns to exclude
   - `OUTPUT_DIRECTORY`: Where to generate docs
   - `PROJECT_NAME`: Project name in documentation

## Generated Documentation

After running Doxygen, you'll find:

- **HTML Documentation**: `docs/doxygen/html/index.html`
- **Warnings Log**: `docs/doxygen/doxygen_warnings.log`

### What's Documented

The following components are fully documented:

- ✅ GP8403 DAC Driver
- ✅ MPU6050 IMU Driver
- ✅ LSM6DS3 IMU Driver
- ✅ System Configuration Functions
- ✅ MCP I/O Expander Configuration
- ✅ OTA Manager
- ✅ Current Loop 4-20mA Driver
- ✅ Modbus TCP
- ✅ WebUI API
- ✅ Log Buffer
- ✅ Main Application

## Optional: Graphviz for Diagrams

To generate class diagrams and call graphs:

1. Install Graphviz: https://graphviz.org/download/
2. Add to PATH: `C:\Program Files\Graphviz\bin`
3. In `Doxyfile`, set:
   ```
   HAVE_DOT = YES
   ```

## Troubleshooting

### "doxygen is not recognized"

- Restart your terminal/IDE after installation
- Verify Doxygen is in your PATH
- Try using full path: `"C:\Program Files\doxygen\bin\doxygen.exe"`

### No Output Generated

- Check that `INPUT` paths in `Doxyfile` are correct
- Verify source files contain Doxygen comments (`/**` blocks)
- Check `doxygen_warnings.log` for issues

### Missing Documentation

- Ensure files have `@file` headers
- Check that functions have `@brief` descriptions
- Verify `EXCLUDE_PATTERNS` doesn't exclude your files

## Viewing Documentation

### Local HTML

Open `docs/doxygen/html/index.html` in any web browser.

### IDE Integration

Most modern IDEs automatically parse Doxygen comments:
- **VS Code**: Install "Doxygen Documentation Generator" extension
- **CLion**: Built-in Doxygen support
- **Visual Studio**: Built-in XML documentation support

Hover over functions to see tooltips with documentation.

## Continuous Integration

To generate docs in CI/CD:

```yaml
# Example GitHub Actions
- name: Install Doxygen
  run: |
    choco install doxygen -y
    # or: winget install Doxygen.Doxygen

- name: Generate Documentation
  run: doxygen Doxyfile

- name: Upload Documentation
  uses: actions/upload-artifact@v3
  with:
    name: documentation
    path: docs/doxygen/html
```

## References

- [Doxygen Manual](https://www.doxygen.nl/manual/index.html)
- [Doxygen Configuration](https://www.doxygen.nl/manual/config.html)
- [Doxygen Special Commands](https://www.doxygen.nl/manual/commands.html)

