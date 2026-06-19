# ESPHome port (WIP — branch `esphome-port`)

An experiment in re-targeting this device to **[ESPHome](https://esphome.io)** for
Home Assistant integration + dashboard OTA. See
[../docs/08-esphome-port.md](../docs/08-esphome-port.md) for the feasibility
assessment, component mapping, and phased plan.

- `knob.yaml` — **Phase-1 hardware scaffold** (display, touch, encoder, button,
  backlight, Wi-Fi, OTA). Not yet hardware-validated; the SH8601 QSPI display
  `init_sequence` still needs porting from `components/bsp/display.c`.

The **Google Cast control** (this project's reason for existing) has no ESPHome
equivalent and is Phase 3: a custom `external_components` C++ port of
`components/cast/`. The ESP-IDF firmware on `main` stays the working reference.

```bash
just esphome-config                # validate (fast)
just esphome-build                 # compile (uvx esphome; first run downloads the toolchain)
just esphome-run                   # build + flash + log (needs the board)
```

> After changing a shared component's IDF dependencies, run
> `uvx esphome clean esphome/knob.yaml` once — an incremental reconfigure can hit a
> spurious "Multiple ways to build … esp_efuse_fields.c.o"; a clean build fixes it.

## Progress (Phase 3)

`cast_controller` runs **real discovery** on a background FreeRTOS task (so it never
blocks ESPHome's main loop), reusing `components/cast/cast_discovery.c` + the IDF
`mdns` component — compile-verified to link. Next: open Cast sessions and expose
now-playing / volume / transport as ESPHome entities for Home Assistant.
