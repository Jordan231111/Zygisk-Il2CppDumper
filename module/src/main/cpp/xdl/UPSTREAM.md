# xDL provenance

Vendored source: [hexhacking/xDL v2.4.0](https://github.com/hexhacking/xDL/tree/6ab03d9d8976b5aa2d264f435ea3a4c4c5b3e67c), commit `6ab03d9d8976b5aa2d264f435ea3a4c4c5b3e67c`.

Files are copied from `xdl/src/main/cpp`, with the upstream MIT license. The project builds the C sources into the module to avoid an additional shared-library dependency. The project's own CMake configuration replaces upstream's standalone-library build.

Updated from 1.2.1: upstream fixes include runtime page alignment, thread-safe initialization, subtraction-based file bounds checks, LZMA lifetime fixes and current Android support. The open/sym/info interfaces used by the dumper remain compatible.

Upstream still assumes linker-owned ELF dynamic tables are well-formed and relies on private system LZMA for `.gnu_debugdata`. It is not a general parser for hostile input. Additional discovery/parsing code must validate externally supplied structures independently; do not pass arbitrary file bytes to xDL.
