# Libraries and licenses

None. Everything is written here (MIT) from published methods, cited in THIRD_PARTY_NOTICES (embedded, `--notices`):
Hack and Jakob (1992) for the vorticity-divergence spectral equations, Williamson et al. (1992) for the test cases,
Arakawa and Lamb (1977) for the C grid. The only vendored files are `cpp/dslvm.hpp` (cpp-dsl-sdk, pinned in `sdk.lock`)
and `cpp/expr.hpp` (lmnum's expression compiler, MIT, same authors). `deps.lock` is empty and `scripts/sync-deps.sh` is a no-op
kept so CI and `test.sh` are uniform with the other hosts. No GPL/LGPL code, no Eigen/FFTW/SHTns (FFTW and SHTns are GPL/CeCILL).
