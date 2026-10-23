#include "sphere.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace lmweather {
namespace {
const double kPi = 3.14159265358979323846;
inline double eps(int n, int m) { return std::sqrt(static_cast<double>(n * n - m * m) / static_cast<double>(4 * n * n - 1)); }

uint64_t splitmix(uint64_t &x) {
  uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}
}  // namespace

void fft(std::vector<cplx> &a, int sign) {
  const size_t n = a.size();
  if (n < 2 || (n & (n - 1))) throw std::invalid_argument("fft length must be a power of two");
  for (size_t i = 1, j = 0; i < n; i++) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    double ang = 2 * kPi / static_cast<double>(len) * sign;
    cplx wl(std::cos(ang), std::sin(ang));
    for (size_t i = 0; i < n; i += len) {
      cplx w(1.0, 0.0);
      for (size_t k = 0; k < len / 2; k++) {
        // recompute the twiddle exactly now and then so long transforms do not accumulate rounding
        if ((k & 15) == 0) w = cplx(std::cos(ang * static_cast<double>(k)), std::sin(ang * static_cast<double>(k)));
        cplx u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

void gauss_legendre(int n, std::vector<double> &x, std::vector<double> &w) {
  x.assign(n, 0);
  w.assign(n, 0);
  for (int i = 0; i < (n + 1) / 2; i++) {
    double z = std::cos(kPi * (i + 0.75) / (n + 0.5)), pp = 1;
    for (int it = 0; it < 100; it++) {
      double p1 = 1, p2 = 0;
      for (int j = 1; j <= n; j++) { double p3 = p2; p2 = p1; p1 = ((2.0 * j - 1) * z * p2 - (j - 1.0) * p3) / j; }
      pp = n * (z * p1 - p2) / (z * z - 1);
      double dz = p1 / pp;
      z -= dz;
      if (std::fabs(dz) < 1e-15) break;
    }
    x[i] = -z;
    x[n - 1 - i] = z;
    w[i] = w[n - 1 - i] = 2 / ((1 - z * z) * pp * pp);
  }
}

void legendre_column(int m, int nmax, double mu, std::vector<double> &out) {
  out.assign(static_cast<size_t>(nmax + 2 - m), 0.0);
  double c = std::sqrt(std::max(0.0, 1 - mu * mu)), p = 1 / std::sqrt(2.0);
  for (int k = 1; k <= m; k++) p *= std::sqrt((2.0 * k + 1) / (2.0 * k)) * c;
  out[0] = p;
  if (nmax + 1 >= m + 1) out[1] = std::sqrt(2.0 * m + 3) * mu * p;
  for (int n = m + 2; n <= nmax + 1; n++) out[n - m] = (mu * out[n - 1 - m] - eps(n - 1, m) * out[n - 2 - m]) / eps(n, m);
}

Sphere::Sphere(int N, const SphereParams &p) : N_(N), p_(p) {
  if (N < 4 || N > 170) throw std::invalid_argument("truncation must be between 4 and 170");
  nlon_ = 4;
  while (nlon_ < 3 * N + 1) nlon_ *= 2;
  nlat_ = nlon_ / 2;
  gauss_legendre(nlat_, mu_, w_);
  P_.resize(nlat_);
  H_.resize(nlat_);
  std::vector<double> col;
  for (int j = 0; j < nlat_; j++) {
    P_[j].resize(N + 1);
    H_[j].resize(N + 1);
    for (int m = 0; m <= N; m++) {
      legendre_column(m, N, mu_[j], col);
      P_[j][m].assign(col.begin(), col.begin() + (N - m + 1));
      H_[j][m].resize(N - m + 1);
      for (int n = m; n <= N; n++) {
        double plus = col[n + 1 - m], minus = (n > m) ? col[n - 1 - m] : 0.0;
        H_[j][m][n - m] = -n * eps(n + 1, m) * plus + (n + 1) * eps(n, m) * minus;
      }
    }
  }
  size_t sz = static_cast<size_t>(N + 1) * (N + 1);
  zeta_.assign(sz, 0);
  delta_.assign(sz, 0);
  phi_.assign(sz, 0);
  hs_grid_.assign(static_cast<size_t>(nlat_) * nlon_, 0.0);
  set_axis_tilt(0);
}

void Sphere::set_orography_grid(const std::vector<double> &hs) {
  if (hs.size() != static_cast<size_t>(nlat_) * nlon_) throw std::invalid_argument("grid size mismatch");
  hs_grid_ = hs;
  has_orography_ = true;
}

void Sphere::set_axis_tilt(double alpha) {
  f_grid_.assign(static_cast<size_t>(nlat_) * nlon_, 0);
  for (int j = 0; j < nlat_; j++)
    for (int k = 0; k < nlon_; k++) {
      double lam = lon(k), c = std::sqrt(1 - mu_[j] * mu_[j]);
      f_grid_[static_cast<size_t>(j) * nlon_ + k] = 2 * p_.omega * (-std::cos(lam) * c * std::sin(alpha) + mu_[j] * std::cos(alpha));
    }
}

double Sphere::lat(int j) const { return std::asin(mu_[j]); }
double Sphere::lon(int k) const { return 2 * kPi * k / nlon_; }

void Sphere::fourier_forward(const double *row, std::vector<cplx> &fm) const {
  std::vector<cplx> a(nlon_);
  for (int k = 0; k < nlon_; k++) a[k] = row[k];
  fft(a, -1);
  fm.resize(N_ + 1);
  for (int m = 0; m <= N_; m++) fm[m] = a[m] / static_cast<double>(nlon_);
}
void Sphere::fourier_backward(const std::vector<cplx> &fm, double *row) const {
  std::vector<cplx> a(nlon_, cplx(0, 0));
  a[0] = cplx(fm[0].real(), 0);
  for (int m = 1; m <= N_; m++) { a[m] = fm[m]; a[nlon_ - m] = std::conj(fm[m]); }
  fft(a, +1);
  for (int k = 0; k < nlon_; k++) row[k] = a[k].real();
}

void Sphere::analyze_scalar(const std::vector<double> &grid, std::vector<cplx> &spec) const {
  spec.assign(zeta_.size(), 0);
  std::vector<cplx> fm;
  for (int j = 0; j < nlat_; j++) {
    fourier_forward(&grid[static_cast<size_t>(j) * nlon_], fm);
    for (int m = 0; m <= N_; m++) {
      const std::vector<double> &P = P_[j][m];
      cplx f = fm[m] * w_[j];
      for (int n = m; n <= N_; n++) spec[idx(m, n)] += f * P[n - m];
    }
  }
}
void Sphere::synthesize_scalar(const std::vector<cplx> &spec, std::vector<double> &grid) const {
  grid.assign(static_cast<size_t>(nlat_) * nlon_, 0);
  std::vector<cplx> fm(N_ + 1);
  for (int j = 0; j < nlat_; j++) {
    for (int m = 0; m <= N_; m++) {
      cplx s = 0;
      const std::vector<double> &P = P_[j][m];
      for (int n = m; n <= N_; n++) s += spec[idx(m, n)] * P[n - m];
      fm[m] = s;
    }
    fourier_backward(fm, &grid[static_cast<size_t>(j) * nlon_]);
  }
}

void Sphere::analyze_divergence(const std::vector<double> &A, const std::vector<double> &B, std::vector<cplx> &spec) const {
  spec.assign(zeta_.size(), 0);
  std::vector<cplx> fa, fb;
  const cplx I(0, 1);
  for (int j = 0; j < nlat_; j++) {
    fourier_forward(&A[static_cast<size_t>(j) * nlon_], fa);
    fourier_forward(&B[static_cast<size_t>(j) * nlon_], fb);
    double wj = w_[j] / (1 - mu_[j] * mu_[j]) / p_.radius;
    for (int m = 0; m <= N_; m++) {
      cplx ca = I * static_cast<double>(m) * fa[m] * wj, cb = fb[m] * wj;
      for (int n = m; n <= N_; n++) spec[idx(m, n)] += ca * P_[j][m][n - m] - cb * H_[j][m][n - m];
    }
  }
}

void Sphere::winds_spectral_to_grid(const std::vector<cplx> &z, const std::vector<cplx> &d, std::vector<double> &U, std::vector<double> &V) const {
  U.assign(static_cast<size_t>(nlat_) * nlon_, 0);
  V.assign(U.size(), 0);
  const double a = p_.radius;
  const cplx I(0, 1);
  std::vector<cplx> psi(z.size(), 0), chi(z.size(), 0);
  for (int m = 0; m <= N_; m++)
    for (int n = std::max(m, 1); n <= N_; n++) {
      double k = -a * a / (n * (n + 1.0));
      psi[idx(m, n)] = z[idx(m, n)] * k;
      chi[idx(m, n)] = d[idx(m, n)] * k;
    }
  std::vector<cplx> fu(N_ + 1), fv(N_ + 1);
  for (int j = 0; j < nlat_; j++) {
    for (int m = 0; m <= N_; m++) {
      cplx sp = 0, sc = 0, hp = 0, hc = 0;
      for (int n = m; n <= N_; n++) {
        double P = P_[j][m][n - m], H = H_[j][m][n - m];
        sp += psi[idx(m, n)] * P; sc += chi[idx(m, n)] * P;
        hp += psi[idx(m, n)] * H; hc += chi[idx(m, n)] * H;
      }
      fu[m] = (I * static_cast<double>(m) * sc - hp) / a;
      fv[m] = (I * static_cast<double>(m) * sp + hc) / a;
    }
    fourier_backward(fu, &U[static_cast<size_t>(j) * nlon_]);
    fourier_backward(fv, &V[static_cast<size_t>(j) * nlon_]);
  }
}

void Sphere::analyze_vort_div(const std::vector<double> &U, const std::vector<double> &V, std::vector<cplx> &z, std::vector<cplx> &d) const {
  std::vector<double> mU(U.size());
  for (size_t i = 0; i < U.size(); i++) mU[i] = -U[i];
  analyze_divergence(V, mU, z);   // zeta = D(V, -U)
  analyze_divergence(U, V, d);    // delta = D(U, V)
  z[idx(0, 0)] = 0;               // the constant is not defined by the winds
  d[idx(0, 0)] = 0;
}

void Sphere::set_state_grid(const std::vector<double> &h, const std::vector<double> &u, const std::vector<double> &v) {
  size_t n = static_cast<size_t>(nlat_) * nlon_;
  if (h.size() != n || u.size() != n || v.size() != n) throw std::invalid_argument("grid size mismatch");
  std::vector<double> U(n), V(n), phi(n);
  for (int j = 0; j < nlat_; j++) {
    double c = std::sqrt(1 - mu_[j] * mu_[j]);
    for (int k = 0; k < nlon_; k++) {
      size_t i = static_cast<size_t>(j) * nlon_ + k;
      U[i] = u[i] * c;
      V[i] = v[i] * c;
      phi[i] = p_.gravity * h[i];
    }
  }
  analyze_vort_div(U, V, zeta_, delta_);
  analyze_scalar(phi, phi_);
  t_ = 0;
  mark_reference();
}

void Sphere::mark_reference() {
  synthesize_scalar(phi_, ref_phi_grid_);
  if (has_tracer_) synthesize_scalar(q_, ref_q_grid_);
}

void Sphere::set_tracer_grid(const std::vector<double> &q) {
  if (q.size() != static_cast<size_t>(nlat_) * nlon_) throw std::invalid_argument("grid size mismatch");
  analyze_scalar(q, q_);
  has_tracer_ = true;
  synthesize_scalar(q_, ref_q_grid_);
}

// dq/dt = -v . grad q = -D(U q, V q) + q delta (the flux form minus the divergence part), so a non-divergent flow
// conserves the mean of q exactly and every flow conserves it to the quadrature error of q delta
void Sphere::tracer_tendency(const std::vector<cplx> &z, const std::vector<cplx> &d, const std::vector<cplx> &q, std::vector<cplx> &dq) const {
  const size_t ng = static_cast<size_t>(nlat_) * nlon_;
  std::vector<double> U, V, qg, dg;
  winds_spectral_to_grid(z, d, U, V);
  synthesize_scalar(q, qg);
  synthesize_scalar(d, dg);
  std::vector<double> A(ng), B(ng), C(ng);
  for (size_t i = 0; i < ng; i++) { A[i] = U[i] * qg[i]; B[i] = V[i] * qg[i]; C[i] = qg[i] * dg[i]; }
  std::vector<cplx> flux, corr;
  analyze_divergence(A, B, flux);
  analyze_scalar(C, corr);
  dq.resize(q.size());
  for (size_t i = 0; i < q.size(); i++) dq[i] = corr[i] - flux[i];
}

std::vector<double> Sphere::grid_tracer() const {
  if (!has_tracer_) throw std::runtime_error("no tracer: set one with tracer() first");
  std::vector<double> g;
  synthesize_scalar(q_, g);
  return g;
}
double Sphere::tracer_mean() const {
  if (!has_tracer_) throw std::runtime_error("no tracer: set one with tracer() first");
  return q_[idx(0, 0)].real() / std::sqrt(2.0);
}
double Sphere::tracer_mass() const {
  std::vector<double> h = grid_height(), q = grid_tracer();
  double s = 0;
  for (int j = 0; j < nlat_; j++) {
    double row = 0;
    for (int k = 0; k < nlon_; k++) { size_t i = static_cast<size_t>(j) * nlon_ + k; row += h[i] * q[i]; }
    s += w_[j] * row / nlon_;
  }
  return 0.5 * s;
}
double Sphere::tracer_min() const { std::vector<double> g = grid_tracer(); return *std::min_element(g.begin(), g.end()); }
double Sphere::tracer_max() const { std::vector<double> g = grid_tracer(); return *std::max_element(g.begin(), g.end()); }
double Sphere::tracer_at(double lat, double lon) const {
  if (!has_tracer_) throw std::runtime_error("no tracer: set one with tracer() first");
  double mu = std::sin(lat);
  std::vector<double> col;
  double sum = 0;
  for (int m = 0; m <= N_; m++) {
    legendre_column(m, N_, mu, col);
    cplx g = 0;
    for (int n = m; n <= N_; n++) g += q_[idx(m, n)] * col[n - m];
    sum += (m == 0 ? 1.0 : 2.0) * (g * std::exp(cplx(0, m * lon))).real();
  }
  return sum;
}

void Sphere::add_mode(int n, int m, double amplitude, double mean_depth) {
  if (m < 0 || n < std::max(m, 1) || n > N_) throw std::invalid_argument("mode (n, m) needs 0 <= m <= n <= truncation and n >= 1");
  std::fill(zeta_.begin(), zeta_.end(), cplx(0, 0));
  std::fill(delta_.begin(), delta_.end(), cplx(0, 0));
  std::fill(phi_.begin(), phi_.end(), cplx(0, 0));
  phi_[idx(0, 0)] = p_.gravity * mean_depth * std::sqrt(2.0);
  // h = amplitude * P_n^m(mu) cos(m lon): the m > 0 harmonic is 2 Re(c e^{i m lon}) so c = amplitude / 2 (m = 0: amplitude)
  phi_[idx(m, n)] += p_.gravity * amplitude * (m == 0 ? 1.0 : 0.5);
  t_ = 0;
  mark_reference();
}

void Sphere::perturb(double amplitude, uint64_t seed, int max_wave) {
  if (max_wave < 1 || max_wave > N_) throw std::invalid_argument("max_wave must be between 1 and the truncation");
  uint64_t st = seed * 0x9E3779B97F4A7C15ULL + 12345;
  auto normal = [&] {  // Irwin-Hall: sum of 12 uniforms - 6, identical on every platform
    double s = 0;
    for (int i = 0; i < 12; i++) s += static_cast<double>(splitmix(st) >> 11) * (1.0 / 9007199254740992.0);
    return s - 6.0;
  };
  std::vector<cplx> add(zeta_.size(), 0);
  for (int m = 0; m <= max_wave; m++)
    for (int n = std::max(m, 1); n <= max_wave; n++) {
      double re = normal(), im = m == 0 ? 0.0 : normal();
      add[idx(m, n)] = cplx(re, im);
    }
  std::vector<double> g;
  synthesize_scalar(add, g);
  double ms = 0, wsum = 0;
  for (int j = 0; j < nlat_; j++) for (int k = 0; k < nlon_; k++) { double x = g[static_cast<size_t>(j) * nlon_ + k]; ms += w_[j] * x * x; wsum += w_[j]; }
  double rms = std::sqrt(ms / wsum);
  double scale = rms > 0 ? amplitude / rms * p_.gravity : 0;
  for (size_t i = 0; i < add.size(); i++) phi_[i] += add[i] * scale;
  t_ = 0;
  mark_reference();
}

void Sphere::tendencies(const std::vector<cplx> &z, const std::vector<cplx> &d, const std::vector<cplx> &f, std::vector<cplx> &dz,
                        std::vector<cplx> &dd, std::vector<cplx> &df) const {
  const size_t ng = static_cast<size_t>(nlat_) * nlon_;
  const double a = p_.radius;
  const cplx I(0, 1);
  std::vector<double> U, V, zg, pg;
  winds_spectral_to_grid(z, d, U, V);
  synthesize_scalar(z, zg);
  synthesize_scalar(f, pg);
  std::vector<double> A(ng), B(ng), UP(ng), VP(ng), PE(ng);
  for (int j = 0; j < nlat_; j++) {
    double om = 1 - mu_[j] * mu_[j];
    for (int k = 0; k < nlon_; k++) {
      size_t i = static_cast<size_t>(j) * nlon_ + k;
      double eta = zg[i] + f_grid_[i];
      A[i] = U[i] * eta;
      B[i] = V[i] * eta;
      UP[i] = U[i] * pg[i];
      VP[i] = V[i] * pg[i];
      PE[i] = pg[i] + p_.gravity * hs_grid_[i] + (U[i] * U[i] + V[i] * V[i]) / (2 * om);  // Bernoulli: g (h + hs) + E
    }
  }
  dz.assign(z.size(), 0);
  dd.assign(z.size(), 0);
  df.assign(z.size(), 0);
  std::vector<cplx> fa, fb, fu, fv, fe;
  for (int j = 0; j < nlat_; j++) {
    size_t o = static_cast<size_t>(j) * nlon_;
    fourier_forward(&A[o], fa);
    fourier_forward(&B[o], fb);
    fourier_forward(&UP[o], fu);
    fourier_forward(&VP[o], fv);
    fourier_forward(&PE[o], fe);
    double wj = w_[j], wd = w_[j] / (1 - mu_[j] * mu_[j]) / a;
    for (int m = 0; m <= N_; m++) {
      cplx ima = I * static_cast<double>(m);
      cplx ca = ima * fa[m] * wd, cb = fb[m] * wd, cu = ima * fu[m] * wd, cv = fv[m] * wd;
      cplx cb2 = ima * fb[m] * wd, ca2 = fa[m] * wd;  // D(B, -A): i m B^m P + A^m H
      cplx ce = fe[m] * wj;
      for (int n = m; n <= N_; n++) {
        double P = P_[j][m][n - m], H = H_[j][m][n - m];
        size_t q = idx(m, n);
        dz[q] -= ca * P - cb * H;                // -D(A, B)
        dd[q] += cb2 * P + ca2 * H;              // +D(B, -A)
        dd[q] += ce * P * (n * (n + 1.0) / (a * a));  // -laplacian(Phi + E)
        df[q] -= cu * P - cv * H;                // -D(U Phi, V Phi)
      }
    }
  }
  dz[idx(0, 0)] = 0;
  dd[idx(0, 0)] = 0;
  df[idx(0, 0)] = 0;
}

void Sphere::filter(double dt) {
  if (p_.diffusion <= 0) return;
  const double a2 = p_.radius * p_.radius;
  for (int m = 0; m <= N_; m++)
    for (int n = std::max(m, 1); n <= N_; n++) {
      double l = n * (n + 1.0);
      double kv = (l - 2) / a2, kp = l / a2;
      double fv = std::exp(-p_.diffusion * kv * kv * dt), fp = std::exp(-p_.diffusion * kp * kp * dt);
      zeta_[idx(m, n)] *= fv;
      delta_[idx(m, n)] *= fv;
      phi_[idx(m, n)] *= fp;
      if (has_tracer_) q_[idx(m, n)] *= fp;
    }
}

void Sphere::step(double dt) {
  using V = std::vector<cplx>;
  // classical RK4 over (zeta, delta, phi) and, when present, the tracer q; in advect-only mode the dynamics are frozen
  struct State { V z, d, f, q; };
  auto rhs = [&](const State &s, State &k) {
    if (advect_only_) { k.z.assign(s.z.size(), 0); k.d.assign(s.z.size(), 0); k.f.assign(s.z.size(), 0); }
    else tendencies(s.z, s.d, s.f, k.z, k.d, k.f);
    if (has_tracer_) tracer_tendency(s.z, s.d, s.q, k.q);
  };
  auto axpy = [](const V &x, const V &k, double c) { V r(x.size()); for (size_t i = 0; i < x.size(); i++) r[i] = x[i] + c * k[i]; return r; };
  auto advance = [&](const State &s, const State &k, double c) {
    State t;
    t.z = axpy(s.z, k.z, c); t.d = axpy(s.d, k.d, c); t.f = axpy(s.f, k.f, c);
    if (has_tracer_) t.q = axpy(s.q, k.q, c);
    return t;
  };
  State s0{zeta_, delta_, phi_, q_}, k1, k2, k3, k4;
  rhs(s0, k1);
  rhs(advance(s0, k1, 0.5 * dt), k2);
  rhs(advance(s0, k2, 0.5 * dt), k3);
  rhs(advance(s0, k3, dt), k4);
  for (size_t i = 0; i < zeta_.size(); i++) {
    zeta_[i] += dt / 6 * (k1.z[i] + 2.0 * k2.z[i] + 2.0 * k3.z[i] + k4.z[i]);
    delta_[i] += dt / 6 * (k1.d[i] + 2.0 * k2.d[i] + 2.0 * k3.d[i] + k4.d[i]);
    phi_[i] += dt / 6 * (k1.f[i] + 2.0 * k2.f[i] + 2.0 * k3.f[i] + k4.f[i]);
    if (has_tracer_) q_[i] += dt / 6 * (k1.q[i] + 2.0 * k2.q[i] + 2.0 * k3.q[i] + k4.q[i]);
  }
  filter(dt);
  t_ += dt;
}

double Sphere::stable_dt(double safety) const {
  std::vector<double> h = grid_height(), u = grid_u(), v = grid_v();
  double hmax = 0, wmax = 0;
  for (size_t i = 0; i < h.size(); i++) { hmax = std::max(hmax, h[i]); wmax = std::max(wmax, std::hypot(u[i], v[i])); }
  if (!(hmax > 0)) throw std::runtime_error("the height field must be positive to step the model");
  // advect-only: no gravity waves and no Coriolis term, the winds alone set the limit (zero winds: nothing moves, any step is fine)
  double omega = advect_only_ ? (N_ / p_.radius) * wmax : (N_ / p_.radius) * (std::sqrt(p_.gravity * hmax) + wmax) + 2 * p_.omega;
  if (!(omega > 0)) return 1e9;
  return safety / omega;
}

void Sphere::check_finite() const {
  // every coefficient: phi(0,0) has zero tendency and a single sample can stay finite while the rest overflows
  double s = 0;
  for (size_t i = 0; i < zeta_.size(); i++) s += std::abs(zeta_[i]) + std::abs(delta_[i]) + std::abs(phi_[i]);
  if (has_tracer_) for (size_t i = 0; i < q_.size(); i++) s += std::abs(q_[i]);
  if (!std::isfinite(s)) throw std::runtime_error("the integration blew up (not finite): use a smaller dt");
}

void Sphere::run(double seconds, double dt) {
  if (!(seconds >= 0)) throw std::invalid_argument("run: seconds must not be negative");
  if (advect_only_ && !has_tracer_) throw std::invalid_argument("advect_only: set a tracer first (Sphere.tracer)");
  if (seconds == 0) return;
  if (!(dt > 0)) dt = stable_dt();
  // RK4 is stable for |omega dt| < 2 sqrt(2); beyond it the result is wrong long before it overflows to NaN
  const double limit = stable_dt(2.0 * std::sqrt(2.0));
  if (!(dt <= limit))
    throw std::invalid_argument("run: dt = " + std::to_string(dt) + " s exceeds the RK4 stability limit of about " + std::to_string(limit) +
                                " s for the present state: use a smaller dt");
  int64_t n = static_cast<int64_t>(std::ceil(seconds / dt - 1e-9));
  if (n < 1) n = 1;
  if (n > 50000000) throw std::invalid_argument("run: more than 5e7 steps; use a larger dt");
  double h = seconds / static_cast<double>(n);
  for (int64_t i = 0; i < n; i++) {
    step(h);
    if ((i & 63) == 63) {
      check_finite();
      // winds that grow during the run (adjustment, instability) lower the limit: a step that was fine at the start
      // can be unstable now, and an unstable RK4 run returns wrong but finite numbers for a long time
      const double now = stable_dt(2.0 * std::sqrt(2.0));
      if (!(h <= now))
        throw std::runtime_error("run: dt = " + std::to_string(h) + " s exceeds the RK4 stability limit of about " + std::to_string(now) +
                                 " s reached at t = " + std::to_string(t_) + " s (the flow accelerated): use a smaller dt");
    }
  }
  check_finite();
}

std::vector<double> Sphere::grid_height() const {
  std::vector<double> g;
  synthesize_scalar(phi_, g);
  for (auto &x : g) x /= p_.gravity;
  return g;
}
std::vector<double> Sphere::grid_vorticity() const {
  std::vector<double> g;
  synthesize_scalar(zeta_, g);
  return g;
}
std::vector<double> Sphere::grid_u() const {
  std::vector<double> U, V;
  winds_spectral_to_grid(zeta_, delta_, U, V);
  for (int j = 0; j < nlat_; j++) { double c = std::sqrt(1 - mu_[j] * mu_[j]); for (int k = 0; k < nlon_; k++) U[static_cast<size_t>(j) * nlon_ + k] /= c; }
  return U;
}
std::vector<double> Sphere::grid_v() const {
  std::vector<double> U, V;
  winds_spectral_to_grid(zeta_, delta_, U, V);
  for (int j = 0; j < nlat_; j++) { double c = std::sqrt(1 - mu_[j] * mu_[j]); for (int k = 0; k < nlon_; k++) V[static_cast<size_t>(j) * nlon_ + k] /= c; }
  return V;
}

double Sphere::mean_height() const { return phi_[idx(0, 0)].real() / std::sqrt(2.0) / p_.gravity; }

double Sphere::energy() const { return kinetic_energy() + potential_energy(); }

double Sphere::kinetic_energy() const {
  std::vector<double> h = grid_height(), u = grid_u(), v = grid_v();
  double s = 0;
  for (int j = 0; j < nlat_; j++) {
    double row = 0;
    for (int k = 0; k < nlon_; k++) { size_t i = static_cast<size_t>(j) * nlon_ + k; row += 0.5 * h[i] * (u[i] * u[i] + v[i] * v[i]); }
    s += w_[j] * row / nlon_;
  }
  return 0.5 * s;
}

double Sphere::potential_energy() const {
  std::vector<double> h = grid_height();
  double s = 0;
  for (int j = 0; j < nlat_; j++) {
    double row = 0;
    for (int k = 0; k < nlon_; k++) { size_t i = static_cast<size_t>(j) * nlon_ + k; row += 0.5 * p_.gravity * h[i] * h[i] + p_.gravity * h[i] * hs_grid_[i]; }
    s += w_[j] * row / nlon_;
  }
  return 0.5 * s;
}

void Sphere::spectrum(std::vector<double> &ke, std::vector<double> &pe) const {
  // area mean of f^2 = 1/2 sum_{n,m} c_m |F_nm|^2 (c_0 = 1, c_m = 2) for the orthonormal P_n^m and the Fourier
  // normalization used here; |v|^2 integrates to sum a^2 (|zeta|^2 + |delta|^2) / (n (n + 1)) (the stream function and
  // velocity potential parts are orthogonal)
  ke.assign(N_ + 1, 0.0);
  pe.assign(N_ + 1, 0.0);
  const double a2 = p_.radius * p_.radius, g2 = p_.gravity * p_.gravity;
  for (int m = 0; m <= N_; m++) {
    double cm = m == 0 ? 1.0 : 2.0;
    for (int n = std::max(m, 1); n <= N_; n++) {
      size_t q = idx(m, n);
      ke[n] += 0.25 * cm * a2 * (std::norm(zeta_[q]) + std::norm(delta_[q])) / (n * (n + 1.0));
      pe[n] += 0.25 * cm * p_.gravity * std::norm(phi_[q]) / g2;
    }
  }
}

double Sphere::potential_enstrophy() const {
  std::vector<double> h = grid_height(), z = grid_vorticity();
  double s = 0;
  for (int j = 0; j < nlat_; j++) {
    double row = 0;
    for (int k = 0; k < nlon_; k++) { size_t i = static_cast<size_t>(j) * nlon_ + k; double f = f_grid_[i]; row += 0.5 * (z[i] + f) * (z[i] + f) / h[i]; }
    s += w_[j] * row / nlon_;
  }
  return 0.5 * s;
}

double Sphere::max_wind() const {
  std::vector<double> u = grid_u(), v = grid_v();
  double m = 0;
  for (size_t i = 0; i < u.size(); i++) m = std::max(m, std::hypot(u[i], v[i]));
  return m;
}

static void errors(const Sphere &s, const std::vector<double> &ref_phi, const std::vector<double> &h, double g, double &l2, double &linf) {
  double num = 0, den = 0, mn = 0, mr = 0;
  const auto &w = s.gauss_weights();
  int nlon = s.nlon();
  for (int j = 0; j < s.nlat(); j++)
    for (int k = 0; k < nlon; k++) {
      size_t i = static_cast<size_t>(j) * nlon + k;
      double r = ref_phi[i] / g, d = h[i] - r;
      num += w[j] * d * d;
      den += w[j] * r * r;
      mn = std::max(mn, std::fabs(d));
      mr = std::max(mr, std::fabs(r));
    }
  l2 = std::sqrt(num / den);
  linf = mn / mr;
}
double Sphere::height_error_l2() const { double a, b; errors(*this, ref_phi_grid_, grid_height(), p_.gravity, a, b); return a; }
double Sphere::height_error_linf() const { double a, b; errors(*this, ref_phi_grid_, grid_height(), p_.gravity, a, b); return b; }
double Sphere::tracer_error_l2() const { double a, b; errors(*this, ref_q_grid_, grid_tracer(), 1.0, a, b); return a; }
double Sphere::tracer_error_linf() const { double a, b; errors(*this, ref_q_grid_, grid_tracer(), 1.0, a, b); return b; }

double Sphere::height_at(double lat, double lon) const {
  double mu = std::sin(lat);
  std::vector<double> col;
  double sum = 0;
  for (int m = 0; m <= N_; m++) {
    legendre_column(m, N_, mu, col);
    cplx g = 0;
    for (int n = m; n <= N_; n++) g += phi_[idx(m, n)] * col[n - m];
    sum += (m == 0 ? 1.0 : 2.0) * (g * std::exp(cplx(0, m * lon))).real();
  }
  return sum / p_.gravity;
}

double Sphere::wind_at(double lat, double lon, bool zonal) const {
  double mu = std::sin(lat), a = p_.radius, c = std::cos(lat);
  std::vector<double> col;
  double sum = 0;
  for (int m = 0; m <= N_; m++) {
    legendre_column(m, N_, mu, col);
    cplx sp = 0, sc = 0, hp = 0, hc = 0;
    for (int n = std::max(m, 1); n <= N_; n++) {
      double k = -a * a / (n * (n + 1.0));
      double P = col[n - m];
      double H = -n * eps(n + 1, m) * col[n + 1 - m] + (n > m ? (n + 1) * eps(n, m) * col[n - 1 - m] : 0.0);
      cplx ps = zeta_[idx(m, n)] * k, ch = delta_[idx(m, n)] * k;
      sp += ps * P; sc += ch * P; hp += ps * H; hc += ch * H;
    }
    cplx im(0, static_cast<double>(m));
    cplx g = zonal ? (im * sc - hp) / a : (im * sp + hc) / a;
    sum += (m == 0 ? 1.0 : 2.0) * (g * std::exp(cplx(0, m * lon))).real();
  }
  return sum / c;
}

double Sphere::vorticity_at(double lat, double lon) const {
  double mu = std::sin(lat);
  std::vector<double> col;
  double sum = 0;
  for (int m = 0; m <= N_; m++) {
    legendre_column(m, N_, mu, col);
    cplx g = 0;
    for (int n = m; n <= N_; n++) g += zeta_[idx(m, n)] * col[n - m];
    sum += (m == 0 ? 1.0 : 2.0) * (g * std::exp(cplx(0, m * lon))).real();
  }
  return sum;
}

double Sphere::mode_coefficient(int n, int m, bool imag) const {
  if (m < 0 || n < m || n > N_) throw std::invalid_argument("mode (n, m) outside the truncation");
  cplx c = phi_[idx(m, n)] / p_.gravity;
  return imag ? c.imag() : c.real();
}

double Sphere::wave_crest(int m, double lat) const {
  if (m < 1 || m > N_) throw std::invalid_argument("zonal wavenumber must be between 1 and the truncation");
  std::vector<double> col;
  legendre_column(m, N_, std::sin(lat), col);
  cplx g = 0;
  for (int n = m; n <= N_; n++) g += phi_[idx(m, n)] * col[n - m];
  return -std::arg(g) / m;
}

}  // namespace lmweather
