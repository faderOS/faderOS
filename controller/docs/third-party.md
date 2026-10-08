# Third-party dependencies and attribution review

The application links against libcurl (OBS WebSocket), OpenSSL (OBS challenge
response), libxml2 (vMix XML), nlohmann/json (JSON) and the system threading library.
Python websockets is used by local test peers. Doxygen is an optional docs tool.
Dependencies are discovered from the system, not vendored in the source bundle.
Review their installed license notices when distributing binaries.

ATEM protocol behavior is implemented independently from documented community
protocol research. Preserve references in ATEM.md. OBS patterns use the official
Luma Wipe filename catalog; this project does not bundle those pattern images.

The physical-control metadata describes Sony BKDS-2010 labels and IDs. The source
bundle contains no Sony ROMs, dumps or manuals. The project license is GNU GPLv3; dependencies retain their own licenses. This
file is a dependency inventory, not a determination of licensing rights.
