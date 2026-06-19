#!/usr/bin/env python3
"""Subset DejaVu Sans for the ESPHome UI and commit it as esphome/fonts/.

The ESPHome `font:` component rasterizes only the glyphs it's asked for, but it
still needs a TTF that *contains* them at build time. Pointing it at the system
`/usr/share/fonts/...` makes the build depend on a DejaVu install (fragile on CI
and other machines). Instead we ship a small subset TTF in the repo.

Coverage = ESPHome's GF_Latin_Core glyphset (the Latin the UI uses) + the Hebrew
letters (song titles / speaker names render via LV_USE_BIDI) + the speaker-list
icons ♪ (U+266A, single speaker) and ☰ (U+2630, Cast group).

Run via `just gen-esphome-font`. Regenerate only when the glyph coverage changes;
the output TTF is committed. Counterpart of scripts/gen_font.py (the vanilla
build's Tiny-TTF subset).
"""
import os
import sys
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont

try:
    import esphome_glyphsets as gs
except ImportError:
    sys.exit("esphome_glyphsets not importable — run via `just gen-esphome-font` "
             "(it uses the esphome tool env), or `uvx --from esphome python ...`.")

SRC = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
OUT = Path("esphome/fonts/DejaVuSans-knob.ttf")


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else SRC
    if not Path(src).exists():
        sys.exit(f"source font not found: {src} (install fonts-dejavu-core)")

    have = set(TTFont(src).getBestCmap().keys())
    cps = set(gs.unicodes_per_glyphset("GF_Latin_Core"))            # UI Latin
    cps |= set(range(0x05D0, 0x05EB)) | {0x05BE, 0x05F3, 0x05F4}    # Hebrew letters + marks
    cps |= {0x266A, 0x2630}                                         # ♪ speaker, ☰ group
    cps &= have                                                     # only present glyphs

    opts = subset.Options()
    opts.flavor = None            # plain TTF
    opts.desubroutinize = True
    opts.recalc_bounds = True
    opts.drop_tables += ["GSUB", "GPOS"]   # no shaping needed for Latin/Hebrew
    opts.name_IDs = ["*"]
    opts.name_legacy = True

    font = subset.load_font(src, opts)
    ss = subset.Subsetter(options=opts)
    ss.populate(unicodes=sorted(cps))
    ss.subset(font)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    subset.save_font(font, OUT, opts)
    print(f"wrote {OUT}: {len(cps)} glyphs, {os.path.getsize(OUT)} bytes "
          f"(source {os.path.getsize(src)} bytes)")


if __name__ == "__main__":
    main()
