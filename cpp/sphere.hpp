// Rotating shallow water on the sphere: spectral transform method (triangular truncation, Gaussian grid),
// vorticity-divergence-geopotential form of Hack and Jakob (1992), classical RK4 time stepping.
#pragma once
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace lmweather {

using cplx = std::complex<double>;

// radix-2 FFT of length n (a power of two); sign -1 = forward (e^{-i...}), +1 = backward, unnormalized
void fft(std::vector<cplx> &a, int sign);
// Gauss-Legendre nodes (ascending, sin(latitude)) and weights (sum 2)
void gauss_legendre(int n, std::vector<double> &x, std::vector<double> &w);
// Normalized associated Legendre P_n^m(mu) (integral of P^2 over [-1,1] is 1) for n = m..nmax+1; out[n-m]
void legendre_column(int m, int nmax, double mu, std::vector<double> &out);

struct SphereParams {
  double radius = 6.37122e6;       // m
  double omega = 7.292e-5;         // rad/s
  double gravity = 9.80616;        // m/s2
  double diffusion = 0;            // del^4 hyperdiffusion coefficient, m4/s (0 = none)
};

class Sphere {
 public:
  Sphere(int truncation, const SphereParams &p);
  int truncation() const { return N_; }
  int nlat() const { return nlat_; }
  int nlon() const { return nlon_; }
  const SphereParams &params() const { return p_; }
  double time() const { return t_; }
  double lat(int j) const;  // radians, south to north
  double lon(int k) const;  // radians

  // Initial conditions on the grid: h (m), u, v (m/s) as functions of (lon, lat) in radians.
  void set_state_grid(const std::vector<double> &h, const std::vector<double> &u, const std::vector<double> &v);
  void set_axis_tilt(double alpha);  // tilt the rotation axis by alpha (rad) as in Williamson test cases 2 and 6 setups: f = 2 Omega (-cos lon cos lat sin a + sin lat cos a)
  void set_diffusion(double nu4) { p_.diffusion = nu4; }
  void mark_reference();  // remember the current height (and tracer) as the reference for *_error_*
  void add_mode(int n, int m, double amplitude, double mean_depth);  // h = mean + amplitude * P_n^m(sin lat) cos(m lon), at rest
  void perturb(double amplitude, uint64_t seed, int max_wave);       // seeded random height field of wavenumbers <= max_wave, zero mean

  // Orography: surface height hs (m) on the grid. h stays the fluid thickness, the free surface is h + hs; the pressure
  // gradient is -grad(g (h + hs) + E) and the energy adds g h hs (Williamson test case 5: flow over an isolated mountain).
  void set_orography_grid(const std::vector<double> &hs);
  bool has_orography() const { return has_orography_; }
  const std::vector<double> &grid_orography() const { return hs_grid_; }

  // Passive tracer q (any unit) carried by the winds: dq/dt = -v . grad q, spectral like the other fields.
  void set_tracer_grid(const std::vector<double> &q);
  bool has_tracer() const { return has_tracer_; }
  void set_advect_only(bool on) { advect_only_ = on; }  // freeze winds and height: only the tracer evolves (Williamson test case 1)
  bool advect_only() const { return advect_only_; }
  std::vector<double> grid_tracer() const;
  double tracer_mean() const;                 // area mean of q: conserved exactly by non-divergent flow (the advective form)
  double tracer_mass() const;                 // area mean of h q: what a divergent flow conserves (to quadrature error)
  double tracer_at(double lat, double lon) const;
  double tracer_min() const;                  // grid extremes: overshoots of the spectral advection show here
  double tracer_max() const;
  double tracer_error_l2() const;             // Williamson normalized errors against the reference tracer
  double tracer_error_linf() const;

  void step(double dt);
  void run(double seconds, double dt);   // splits into equal steps; dt <= 0 picks a stable one
  double stable_dt(double safety = 1.5) const;

  // diagnostics (area means unless stated)
  double mean_height() const;            // exactly conserved
  double energy() const;                 // J/m2: mean of h|v|^2/2 + g h^2/2 (times rho = 1)
  double potential_enstrophy() const;
  double max_wind() const;
  double height_error_l2() const;        // Williamson normalized l2 error against the reference height
  double height_error_linf() const;
  double height_at(double lat, double lon) const;
  double wind_at(double lat, double lon, bool zonal) const;
  double vorticity_at(double lat, double lon) const;
  double mode_coefficient(int n, int m, bool imag) const;  // coefficient of h's spectral component (n, m)
  double wave_crest(int m, double lat) const;             // longitude (rad) of a crest of the zonal harmonic m of h at a latitude

  // grid fields (nlat x nlon, row = latitude south to north)
  std::vector<double> grid_height() const;
  std::vector<double> grid_u() const;
  std::vector<double> grid_v() const;
  std::vector<double> grid_vorticity() const;
  const std::vector<double> &gauss_weights() const { return w_; }
  const std::vector<double> &gauss_mu() const { return mu_; }

  // exposed for tests
  void analyze_scalar(const std::vector<double> &grid, std::vector<cplx> &spec) const;
  void synthesize_scalar(const std::vector<cplx> &spec, std::vector<double> &grid) const;
  void analyze_divergence(const std::vector<double> &A, const std::vector<double> &B, std::vector<cplx> &spec) const;

 private:
  int N_, nlat_, nlon_;
  SphereParams p_;
  double t_ = 0;
  std::vector<double> mu_, w_;
  std::vector<std::vector<std::vector<double>>> P_, H_;  // [lat][m][n-m]
  std::vector<cplx> zeta_, delta_, phi_, q_;              // spectral, index m*(N+1)+n; q_ only with a tracer
  bool has_tracer_ = false, advect_only_ = false, has_orography_ = false;
  std::vector<double> ref_phi_grid_, ref_q_grid_, f_grid_, hs_grid_;  // f_grid_: Coriolis parameter on the grid; hs_grid_: surface height
  size_t idx(int m, int n) const { return static_cast<size_t>(m) * (N_ + 1) + n; }
  void tracer_tendency(const std::vector<cplx> &z, const std::vector<cplx> &d, const std::vector<cplx> &q, std::vector<cplx> &dq) const;
  // grid <-> Fourier at one latitude
  void fourier_forward(const double *row, std::vector<cplx> &fm) const;  // m = 0..N
  void fourier_backward(const std::vector<cplx> &fm, double *row) const;
  void tendencies(const std::vector<cplx> &z, const std::vector<cplx> &d, const std::vector<cplx> &f, std::vector<cplx> &dz,
                  std::vector<cplx> &dd, std::vector<cplx> &df) const;
  void winds_spectral_to_grid(const std::vector<cplx> &z, const std::vector<cplx> &d, std::vector<double> &U, std::vector<double> &V) const;
  void analyze_vort_div(const std::vector<double> &U, const std::vector<double> &V, std::vector<cplx> &z, std::vector<cplx> &d) const;
  void filter(double dt);
  void check_finite() const;
};

}  // namespace lmweather
