# Examples

`examples/*.lms` assert their own results and write CSVs under `build/examples/`; `examples/combo/*.lms` integrate then draw with
lmplot. Notes from writing them:

- The DSL has no exponent literals, `exp`, `log`, ternary or `mkdir`; strings are built in loops and written with `wx.save_text`;
  `test.sh` creates `build/examples` (the combo scripts call lmscript's `mkdir`).
- TC2 and TC6 need the right Coriolis axis: TC2 with tilted flow is steady only when f is tilted with it (found the hard way:
  a first version without the tilt drifted by 20% in a day).
- Crest positions are modulo the wavelength, so every phase comparison unwraps (`d - floor(d/period + 0.5) * period`).
- Plane tests need walls in mind: a bump varying in x with y-uniform fields forces v = 0 at the walls, which is not the
  geostrophic state; the adjustment test varies in y (u geostrophic along the channel), where walls are harmless.
