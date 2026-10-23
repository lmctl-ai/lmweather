// Native regression tests of the numerical core: transforms, quadrature, spherical harmonics, conservation.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "plane.hpp"
#include "sphere.hpp"

using namespace lmweather;
static int failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
  } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

int main() {
  const double pi = 3.14159265358979323846;
  // FFT against the naive transform, and the inverse
  {
    const int n = 64;
    std::vector<cplx> a(n), ref(n);
    for (int k = 0; k < n; k++) a[k] = cplx(std::sin(0.3 * k) + 0.1 * k, std::cos(1.7 * k));
    for (int m = 0; m < n; m++) {
      cplx s = 0;
      for (int k = 0; k < n; k++) s += a[k] * std::exp(cplx(0, -2 * pi * m * k / n));
      ref[m] = s;
    }
    std::vector<cplx> b = a;
    fft(b, -1);
    for (int m = 0; m < n; m++) NEAR(std::abs(b[m] - ref[m]), 0.0, 1e-10);
    fft(b, +1);
    for (int k = 0; k < n; k++) NEAR(std::abs(b[k] / static_cast<double>(n) - a[k]), 0.0, 1e-12);
    bool threw = false;
    try { std::vector<cplx> c(12); fft(c, -1); } catch (const std::exception &) { threw = true; }
    CHECK(threw);
  }
  // Gauss-Legendre: weights sum to 2, exact for degree 2n-1, nodes symmetric and ascending
  {
    std::vector<double> x, w;
    gauss_legendre(32, x, w);
    double s = 0, p10 = 0, p63 = 0;
    for (int i = 0; i < 32; i++) { s += w[i]; p10 += w[i] * std::pow(x[i], 10); p63 += w[i] * std::pow(x[i], 62); if (i) CHECK(x[i] > x[i - 1]); NEAR(x[i], -x[31 - i], 1e-14); }
    NEAR(s, 2.0, 1e-13);
    NEAR(p10, 2.0 / 11, 1e-13);
    NEAR(p63, 2.0 / 63, 1e-12);
  }
  // normalized associated Legendre functions are orthonormal under Gaussian quadrature
  {
    std::vector<double> x, w, ca, cb;
    gauss_legendre(40, x, w);
    for (int m = 0; m <= 6; m++)
      for (int n1 = m; n1 <= 12; n1++)
        for (int n2 = m; n2 <= 12; n2++) {
          double s = 0;
          for (int i = 0; i < 40; i++) { legendre_column(m, 12, x[i], ca); s += w[i] * ca[n1 - m] * ca[n2 - m]; }
          NEAR(s, n1 == n2 ? 1.0 : 0.0, 1e-12);
        }
  }
  // spectral round trip and the divergence/vorticity machinery
  {
    SphereParams p;
    Sphere s(21, p);
    std::vector<cplx> spec(22 * 22, 0), back;
    unsigned seed = 5;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0 - 0.5; };
    for (int m = 0; m <= 21; m++) for (int n = m; n <= 21; n++) spec[m * 22 + n] = cplx(rnd(), m == 0 ? 0.0 : rnd());
    std::vector<double> grid;
    s.synthesize_scalar(spec, grid);
    s.analyze_scalar(grid, back);
    for (size_t i = 0; i < spec.size(); i++) NEAR(std::abs(back[i] - spec[i]), 0.0, 1e-12);
    // solid-body rotation u = u0 cos(lat): zeta = 2 u0 sin(lat) / a, divergence 0
    int nlat = s.nlat(), nlon = s.nlon();
    std::vector<double> h(nlat * nlon, 1000.0), u(h), v(nlat * nlon, 0.0);
    for (int j = 0; j < nlat; j++) for (int k = 0; k < nlon; k++) u[j * nlon + k] = 10 * std::cos(s.lat(j));
    s.set_state_grid(h, u, v);
    NEAR(s.vorticity_at(0.7, 1.0), 2 * 10 * std::sin(0.7) / p.radius, 1e-12);
    NEAR(s.wind_at(0.7, 2.0, true), 10 * std::cos(0.7), 1e-9);
    NEAR(s.wind_at(0.7, 2.0, false), 0.0, 1e-9);
    NEAR(s.height_at(-0.3, 4.0), 1000.0, 1e-9);
    NEAR(s.mean_height(), 1000.0, 1e-9);
    NEAR(s.energy(), 0.5 * 1000 * (100.0 * 2.0 / 3.0) + 0.5 * p.gravity * 1e6, 1e-6 * s.energy());
  }
  // a state at rest stays at rest; mass is conserved by the step
  {
    SphereParams p;
    Sphere s(10, p);
    std::vector<double> h(s.nlat() * s.nlon(), 5000.0), z(h.size(), 0.0);
    s.set_state_grid(h, z, z);
    s.run(86400.0, 1800.0);
    NEAR(s.max_wind(), 0.0, 1e-9);
    NEAR(s.height_at(0.4, 1.0), 5000.0, 1e-8);
    s.perturb(20.0, 3, 5);
    double m0 = s.mean_height();
    s.run(43200.0, 1800.0);
    NEAR(s.mean_height(), m0, 1e-9);
    bool threw = false;
    try { s.perturb(1.0, 1, 0); } catch (const std::exception &) { threw = true; }
    CHECK(threw);
  }
  // plane: gravity wave speed, mass, bad parameters
  {
    PlaneParams q;
    q.nx = 100; q.ny = 8; q.lx = 1.0e6; q.ly = 2.0e4; q.depth = 100; q.linear = true;
    Plane pl(q);
    double k = 2 * pi / q.lx, c = std::sqrt(q.gravity * q.depth);
    for (int j = 0; j < q.ny; j++) for (int i = 0; i < q.nx; i++) pl.h(i, j) = q.depth + std::cos(k * pl.x_center(i));
    pl.run(pi / (c * k), 0);
    NEAR(pl.h_at(0.5 * pl.dx(), 1e4) - q.depth, -std::cos(k * 0.5 * pl.dx()), 2e-3);
    NEAR(pl.mean_depth(), q.depth, 1e-10);
    bool threw = false;
    try { PlaneParams bad; bad.nx = 2; bad.ny = 2; bad.lx = 1; bad.ly = 1; Plane b(bad); } catch (const std::exception &) { threw = true; }
    CHECK(threw);
    threw = false;
    try { Sphere s(2, SphereParams{}); } catch (const std::exception &) { threw = true; }
    CHECK(threw);
  }
  if (failures) { std::fprintf(stderr, "%d failures\n", failures); return 1; }
  std::puts("core tests passed");
  return 0;
}
