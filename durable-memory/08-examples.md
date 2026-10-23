# Examples

`examples/*.lms` assert their own results and write CSVs under `build/examples/`; `examples/gallery/*.lms` integrate with
`lmweather` and draw with `lmplot`. Notes from writing them:

- The DSL has no exponent literals, `exp`, `log`, ternary or `mkdir`; strings are built in loops and written with `wx.save_text`;
  each example calls lmscript's `mkdir` before running the host.
- TC2 and TC6 need the right Coriolis axis: TC2 with tilted flow is steady only when f is tilted with it (found the hard way:
  a first version without the tilt drifted by 20% in a day).
- Crest positions are modulo the wavelength, so every phase comparison unwraps (`d - floor(d/period + 0.5) * period`).
- Plane tests need walls in mind: a bump varying in x with y-uniform fields forces v = 0 at the walls, which is not the
  geostrophic state; the adjustment test varies in y (u geostrophic along the channel), where walls are harmless.

## Gallery (pictures) and `lmctl autorun`

`examples/gallery/*.lms` each compute with `lmweather`, write CSV into `data/`, and draw with `lmplot` into `images/` in the
*current directory*; the README gallery shows copies in `docs/images/` (`examples/render-all.sh` runs them from a checkout and copies;
PNG <= 300 KB, GIF <= 3 MB). Every example, old and new, ends with `r = system ${dsl_cli(p)} --dsl ${out}; assert(r.code == 0);`
so `lmctl autorun <raw github url>` produces the output, not just bytecode. README captions include the `lmctl autorun` command.
- Stories: Rossby-Haurwitz wave 4 (still T42 map and 56-frame 14-day GIF via ffmpeg palette), equatorial beta-plane Kelvin wave and
  off-equatorial n = 1 Rossby lobes (ENSO warm-pool pulse), mid-latitude barotropic jet instability and vorticity roll-up on the sphere,
  Rossby geostrophic adjustment against the Fourier-cosine potential-vorticity prediction, beta-plane Rossby wave westward phase drift,
  D'Alembert shallow-water gravity wave splitting and space-time Hovmoller cone, Williamson test case 2 cross-polar tilted geostrophic
  flow, planetary rotation and Rhines-scale banded jets (`wx.planet`), and spherical-harmonic Laplace tidal eigenfrequencies.
- Second set (20 gallery examples in all), grouped in the README by theme: Williamson TC1 bell advection at three truncations
  (`tracer`, `advect_only`; the error is edge ringing, not transport), TC5 mountain flow still and 45-frame GIF (`orography`),
  RK4 step sweep (dt^4 error, refused step above 2 sqrt(2)/omega), channel jet roll-up GIF, Bickley jet growth rate per
  wavenumber (`Plane.harmonic_energy`, Lipps 1962 cutoff kL = 2), decaying turbulence n^-3 spectrum (`save_spectrum`),
  Rossby dispersion relation measured for m = 1..7, adjustment width study L/Ld = 0.25..4, equatorial Kelvin GIF.
- `test.sh` checks README images (magic bytes, sizes) and runs the gallery when `lmplot` is available (`*_anim` only with `ffmpeg`).
- The five plain `examples/*.lms` capture the host's stdout (`r = system ...`) and echo it, so `lmctl autorun` shows the
  checked numbers; without the echo a first-time visitor saw only "wrote build/x.dslb".
- Figure hygiene learned the hard way: multiplot panel titles above ~55 characters clip at 1000 px; keys with long
  entries overflow a half-width panel (shorten the titles, or `set key below`); check every PNG and a late GIF frame.
