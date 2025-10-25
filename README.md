# Grayscale PPM Converter (P3)

This program (`grayscale.c`) converts a color ASCII PPM image (format P3) to grayscale. It robustly parses headers (skips `#` comments and arbitrary whitespace), validates input, and writes a P3 grayscale image. Processing uses buffered I/O and a small lookup table for speed.

## Requirements
- C11-compatible compiler (e.g., GCC/Clang)
- `make` (optional, recommended)

## Build

Using the provided Makefile:

```bash
make
```

Or compile directly:

```bash
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -o grayscale grayscale.c
```

Windows notes:
- With MSYS2/MinGW, use the same `make`/`gcc` commands in the MSYS2 shell.
- With MinGW without MSYS, use `mingw32-make` instead of `make`.

## Usage
1. Place your input PPM file (`im.ppm`) next to the executable. The file must be ASCII PPM (P3) with a maximum color value of 255.
2. Run the program:

   - Linux/macOS:
     ```bash
     ./grayscale
     ```
   - Windows (PowerShell or CMD):
     ```powershell
     .\grayscale.exe
     ```

3. The output file `im-gray.ppm` will be created in the same directory.

## Customizing input/output filenames
The defaults are defined at the top of `grayscale.c`:

```c
#define INPUT_FILE "im.ppm"
#define OUTPUT_FILE "im-gray.ppm"
```

Edit these defines and rebuild to change the input/output paths.

## Format and limitations
- Supports P3 (ASCII) PPM only; P6 (binary) is not supported.
- Maximum color value must be 255.
- Image dimensions are validated; overly large images are rejected.
- Grayscale is computed as the simple average: `(r + g + b) / 3`.

## Example
With the provided `im.ppm`:

```bash
make
./grayscale    # or .\grayscale.exe on Windows
```

Result: `im-gray.ppm` is written next to the executable.


