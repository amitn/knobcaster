# Vendored cJSON (host tests only)

`cJSON.c` / `cJSON.h` are vendored from the `espressif/cjson` managed component
so the **native** unit tests (`just test` → `pio test -e native`) can compile the
pure `cast_status.c` parsers on the host. The firmware build uses the managed
component, not this copy. See [docs/07-testing.md](../../docs/07-testing.md).
