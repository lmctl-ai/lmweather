#include "plane.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <string>

namespace lmweather {
namespace {
const double kPi = 3.14159265358979323846;
uint64_t splitmix(uint64_t &x) {
  uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}
}  // namespace

Plane::Plane(const PlaneParams &p) : p_(p) {
  if (p.nx < 4 || p.ny < 4 || p.nx > 4000 || p.ny > 4000 || static_cast<int64_t>(p.nx) * p.ny > 8000000)
    throw std::invalid_argument("grid must be at least 4 x 4 and at most 8 million cells");
  if (!(p.lx > 0 && p.ly > 0)) throw std::invalid_argument("domain sizes must be positive");
  reset();
}

void Plane::reset() {
  h_.assign(static_cast<size_t>(p_.nx) * p_.ny, p_.depth);
  u_.assign(h_.size(), 0);
  v_.assign(static_cast<size_t>(p_.nx) * (p_.ny + 1), 0);
  t_ = 0;
}

void Plane::perturb(double amplitude, uint64_t seed) {
  uint64_t st = seed * 0x9E3779B97F4A7C15ULL + 777;
  std::vector<double> n(h_.size()), t(h_.size());
  for (auto &x : n) {
    double s = 0;
    for (int i = 0; i < 12; i++) s += static_cast<double>(splitmix(st) >> 11) * (1.0 / 9007199254740992.0);
    x = s - 6.0;
  }
  const int nx = p_.nx, ny = p_.ny;
  for (int pass = 0; pass < 3; pass++) {  // 1-2-1 smoothing in both directions (periodic in x, mirrored in y)
    for (int j = 0; j < ny; j++)
      for (int i = 0; i < nx; i++) t[static_cast<size_t>(j) * nx + i] = 0.25 * n[static_cast<size_t>(j) * nx + (i + nx - 1) % nx] + 0.5 * n[static_cast<size_t>(j) * nx + i] + 0.25 * n[static_cast<size_t>(j) * nx + (i + 1) % nx];
    for (int j = 0; j < ny; j++)
      for (int i = 0; i < nx; i++)
        n[static_cast<size_t>(j) * nx + i] = 0.25 * t[static_cast<size_t>(std::max(j - 1, 0)) * nx + i] + 0.5 * t[static_cast<size_t>(j) * nx + i] + 0.25 * t[static_cast<size_t>(std::min(j + 1, ny - 1)) * nx + i];
  }
  double mean = 0, ms = 0;
  for (double x : n) mean += x;
  mean /= n.size();
  for (double &x : n) { x -= mean; ms += x * x; }
  double rms = std::sqrt(ms / n.size());
  double scale = rms > 0 ? amplitude / rms : 0;
  for (size_t i = 0; i < h_.size(); i++) h_[i] += n[i] * scale;
}

void Plane::tendency(const std::vector<double> &h, const std::vector<double> &u, const std::vector<double> &v, std::vector<double> &dh,
                     std::vector<double> &du, std::vector<double> &dv) const {
  const int nx = p_.nx, ny = p_.ny;
  const double dxx = dx(), dyy = dy(), g = p_.gravity;
  auto H = [&](int i, int j) { return h[static_cast<size_t>(j) * nx + i]; };
  auto U = [&](int i, int j) { return u[static_cast<size_t>(j) * nx + ((i % nx) + nx) % nx]; };
  auto V = [&](int i, int j) { return v[static_cast<size_t>(j) * nx + ((i % nx) + nx) % nx]; };
  dh.assign(h.size(), 0);
  du.assign(u.size(), 0);
  dv.assign(v.size(), 0);
  const bool lin = p_.linear;
  // bernoulli function B = g h + K at centers
  std::vector<double> B(h.size());
  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++) {
      double k = 0;
      if (!lin) k = 0.5 * (0.5 * (U(i, j) * U(i, j) + U(i + 1, j) * U(i + 1, j)) + 0.5 * (V(i, j) * V(i, j) + V(i, j + 1) * V(i, j + 1)));
      B[static_cast<size_t>(j) * nx + i] = g * H(i, j) + k;
    }
  auto Bf = [&](int i, int j) { return B[static_cast<size_t>(j) * nx + ((i % nx) + nx) % nx]; };
  // absolute vorticity at corners (i, j): corner at the west-south of cell (i, j), j = 0..ny
  std::vector<double> eta(static_cast<size_t>(nx) * (ny + 1));
  for (int j = 0; j <= ny; j++) {
    double f = coriolis(j * dyy);
    for (int i = 0; i < nx; i++) {
      double zeta = 0;
      if (!lin) {
        double dvdx = (V(i, j) - V(i - 1, j)) / dxx;
        double uj = j < ny ? U(i, j) : U(i, ny - 1), ujm = j > 0 ? U(i, j - 1) : U(i, 0);  // free-slip: du/dy = 0 at the walls
        zeta = dvdx - (uj - ujm) / dyy;
      }
      eta[static_cast<size_t>(j) * nx + i] = f + zeta;
    }
  }
  auto E = [&](int i, int j) { return eta[static_cast<size_t>(j) * nx + ((i % nx) + nx) % nx]; };
  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++) {
      // u point (i, j): west face of cell (i, j)
      double q = 0.5 * (E(i, j) + E(i, j + 1));
      double vbar = 0.25 * (V(i - 1, j) + V(i, j) + V(i - 1, j + 1) + V(i, j + 1));
      double r = q * vbar - (Bf(i, j) - Bf(i - 1, j)) / dxx - p_.drag * U(i, j);
      if (p_.viscosity != 0) {
        double uy1 = j + 1 < ny ? U(i, j + 1) : U(i, j), uy0 = j > 0 ? U(i, j - 1) : U(i, j);
        r += p_.viscosity * ((U(i + 1, j) - 2 * U(i, j) + U(i - 1, j)) / (dxx * dxx) + (uy1 - 2 * U(i, j) + uy0) / (dyy * dyy));
      }
      du[static_cast<size_t>(j) * nx + i] = r;
    }
  for (int j = 1; j < ny; j++)
    for (int i = 0; i < nx; i++) {
      // v point (i, j): south face of cell (i, j)
      double q = 0.5 * (E(i, j) + E(i + 1, j));
      double ubar = 0.25 * (U(i, j - 1) + U(i + 1, j - 1) + U(i, j) + U(i + 1, j));
      double r = -q * ubar - (Bf(i, j) - Bf(i, j - 1)) / dyy - p_.drag * V(i, j);
      if (p_.viscosity != 0) {
        double vy1 = j + 1 < ny ? V(i, j + 1) : 0.0, vy0 = j - 1 > 0 ? V(i, j - 1) : 0.0;
        r += p_.viscosity * ((V(i + 1, j) - 2 * V(i, j) + V(i - 1, j)) / (dxx * dxx) + (vy1 - 2 * V(i, j) + vy0) / (dyy * dyy));
      }
      dv[static_cast<size_t>(j) * nx + i] = r;
    }
  // continuity in flux form
  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++) {
      auto hu = [&](int ii) { return lin ? p_.depth : 0.5 * (H(ii - 1 < 0 ? nx - 1 : ii - 1, j) + H(ii % nx, j)); };
      double fe = (lin ? p_.depth : 0.5 * (H(i, j) + H((i + 1) % nx, j))) * U(i + 1, j);
      double fw = hu(i) * U(i, j);
      double fn = 0, fs = 0;
      if (j + 1 < ny) fn = (lin ? p_.depth : 0.5 * (H(i, j) + H(i, j + 1))) * V(i, j + 1);
      if (j > 0) fs = (lin ? p_.depth : 0.5 * (H(i, j - 1) + H(i, j))) * V(i, j);
      dh[static_cast<size_t>(j) * nx + i] = -((fe - fw) / dxx + (fn - fs) / dyy);
    }
}

double Plane::stable_dt(double safety) const {
  // the linear equations carry waves at sqrt(g H) on the resting depth and have no advection: the winds that
  // develop do not change their stability limit (the nonlinear limit shrinks with the fastest current and the deepest column)
  double hmax = p_.linear ? p_.depth : 0, umax = p_.linear ? 0 : max_speed();
  if (!p_.linear) for (double x : h_) hmax = std::max(hmax, x);
  if (!(hmax > 0)) throw std::runtime_error("the depth must be positive to step the model");
  double c = std::sqrt(p_.gravity * hmax) + umax;
  double inv = c * std::sqrt(1.0 / (dx() * dx()) + 1.0 / (dy() * dy()));
  double visc = p_.viscosity * (4.0 / (dx() * dx()) + 4.0 / (dy() * dy()));
  return safety * 2.0 / (inv + visc + std::fabs(p_.f0) + std::fabs(p_.beta) * p_.ly);
}

void Plane::step(double dt) {
  using Vec = std::vector<double>;
  Vec h1, u1, v1, h2, u2, v2, h3, u3, v3, h4, u4, v4;
  auto ax = [](const Vec &x, const Vec &k, double c) { Vec r(x.size()); for (size_t i = 0; i < x.size(); i++) r[i] = x[i] + c * k[i]; return r; };
  tendency(h_, u_, v_, h1, u1, v1);
  Vec ht = ax(h_, h1, 0.5 * dt), ut = ax(u_, u1, 0.5 * dt), vt = ax(v_, v1, 0.5 * dt);
  tendency(ht, ut, vt, h2, u2, v2);
  ht = ax(h_, h2, 0.5 * dt); ut = ax(u_, u2, 0.5 * dt); vt = ax(v_, v2, 0.5 * dt);
  tendency(ht, ut, vt, h3, u3, v3);
  ht = ax(h_, h3, dt); ut = ax(u_, u3, dt); vt = ax(v_, v3, dt);
  tendency(ht, ut, vt, h4, u4, v4);
  for (size_t i = 0; i < h_.size(); i++) h_[i] += dt / 6 * (h1[i] + 2 * h2[i] + 2 * h3[i] + h4[i]);
  for (size_t i = 0; i < u_.size(); i++) u_[i] += dt / 6 * (u1[i] + 2 * u2[i] + 2 * u3[i] + u4[i]);
  for (size_t i = 0; i < v_.size(); i++) v_[i] += dt / 6 * (v1[i] + 2 * v2[i] + 2 * v3[i] + v4[i]);
  t_ += dt;
}

void Plane::run(double seconds, double dt) {
  if (!(seconds >= 0)) throw std::invalid_argument("run: seconds must not be negative");
  if (seconds == 0) return;
  if (!(dt > 0)) dt = stable_dt();
  // stable_dt(s) = 2 s / (c sqrt(1/dx^2 + 1/dy^2) + ...) and the fastest C-grid gravity wave has omega = 2 c sqrt(...):
  // RK4 (|omega dt| < 2 sqrt(2)) needs s <= sqrt(2) / 2
  const double limit = stable_dt(std::sqrt(0.5));
  if (!(dt <= limit))
    throw std::invalid_argument("run: dt = " + std::to_string(dt) + " s exceeds the RK4 stability limit of about " + std::to_string(limit) +
                                " s for the present state: use a smaller dt");
  int64_t n = static_cast<int64_t>(std::ceil(seconds / dt - 1e-9));
  if (n < 1) n = 1;
  if (n > 200000000) throw std::invalid_argument("run: too many steps; use a larger dt");
  double hstep = seconds / static_cast<double>(n);
  for (int64_t i = 0; i < n; i++) {
    step(hstep);
    if ((i & 63) == 63 || i + 1 == n) {  // all fields: a local blow-up needs many steps to reach any single point
      double s = 0;
      for (double x : h_) s += x;
      for (double x : u_) s += x;
      for (double x : v_) s += x;
      if (!std::isfinite(s)) throw std::runtime_error("the integration blew up (not finite): use a smaller dt");
      // the limit falls as the flow accelerates or the layer deepens: re-check it, an unstable RK4 run stays finite for long
      const double now = stable_dt(std::sqrt(0.5));
      if (!(hstep <= now))
        throw std::runtime_error("run: dt = " + std::to_string(hstep) + " s exceeds the RK4 stability limit of about " + std::to_string(now) +
                                 " s reached at t = " + std::to_string(t_) + " s (the flow accelerated): use a smaller dt");
    }
  }
}

double Plane::mean_depth() const {
  double s = 0;
  for (double x : h_) s += x;
  return s / h_.size();
}

double Plane::energy() const {
  const int nx = p_.nx, ny = p_.ny;
  double s = 0;
  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++) {
      double uc = 0.5 * (u(i, j) + u((i + 1) % nx, j)), vc = 0.5 * (v(i, j) + v(i, j + 1));
      double hh = h(i, j);
      s += 0.5 * hh * (uc * uc + vc * vc) + 0.5 * p_.gravity * hh * hh;
    }
  return s / h_.size();
}

double Plane::max_speed() const {
  const int nx = p_.nx, ny = p_.ny;
  double m = 0;
  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++) {
      double uc = 0.5 * (u(i, j) + u((i + 1) % nx, j)), vc = 0.5 * (v(i, j) + v(i, j + 1));
      m = std::max(m, std::hypot(uc, vc));
    }
  return m;
}

double Plane::zeta_at_corner(int i, int j) const {
  const int nx = p_.nx, ny = p_.ny;
  int im = (i + nx - 1) % nx;
  double dvdx = (v(i % nx, j) - v(im, j)) / dx();
  double uj = j < ny ? u(i % nx, j) : u(i % nx, ny - 1), ujm = j > 0 ? u(i % nx, j - 1) : u(i % nx, 0);
  return dvdx - (uj - ujm) / dy();
}

double Plane::potential_vorticity_anomaly_rms() const {
  // q' = zeta - f0 (h - H) / H at interior corners (h averaged over the four cells around the corner): the
  // combination the discrete linear equations conserve at every corner
  const int nx = p_.nx, ny = p_.ny;
  double s = 0;
  size_t cnt = 0;
  for (int j = 1; j < ny; j++)
    for (int i = 0; i < nx; i++) {
      int im = (i + nx - 1) % nx;
      double hav = 0.25 * (h(im, j - 1) + h(i, j - 1) + h(im, j) + h(i, j));
      double q = zeta_at_corner(i, j) - p_.f0 * (hav - p_.depth) / p_.depth;
      s += q * q;
      cnt++;
    }
  return std::sqrt(s / static_cast<double>(cnt));
}

static double interp(const Plane &pl, double x, double y, int kind) {  // kind 0 h, 1 u, 2 v: bilinear on each field's own points
  const auto &p = pl.params();
  double dxx = p.lx / p.nx, dyy = p.ly / p.ny;
  double xs = kind == 1 ? 0.0 : 0.5, ys = kind == 2 ? 0.0 : 0.5;  // offset of the first point in cells
  double fx = x / dxx - xs, fy = y / dyy - ys;
  int rows = kind == 2 ? p.ny + 1 : p.ny;
  double fl = std::floor(fx);
  int i0 = static_cast<int>(fl), j0 = static_cast<int>(std::floor(fy));
  double ax = fx - fl, ay = fy - std::floor(fy);
  auto get = [&](int i, int j) {
    i = ((i % p.nx) + p.nx) % p.nx;
    j = std::max(0, std::min(rows - 1, j));
    return kind == 0 ? pl.h(i, j) : kind == 1 ? pl.u(i, j) : pl.v(i, j);
  };
  return (1 - ax) * ((1 - ay) * get(i0, j0) + ay * get(i0, j0 + 1)) + ax * ((1 - ay) * get(i0 + 1, j0) + ay * get(i0 + 1, j0 + 1));
}
double Plane::h_at(double x, double y) const { return interp(*this, x, y, 0); }
double Plane::u_at(double x, double y) const { return interp(*this, x, y, 1); }
double Plane::v_at(double x, double y) const { return interp(*this, x, y, 2); }

static std::complex<double> harmonic(int nx, int m, const std::vector<double> &row, double shift_cells) {
  std::complex<double> c = 0;
  for (int i = 0; i < nx; i++) c += row[i] * std::exp(std::complex<double>(0, -2 * kPi * m * (i + shift_cells) / nx));
  return c / static_cast<double>(nx);
}
double Plane::zonal_amplitude(int m, int j) const {
  if (m < 1 || m >= p_.nx / 2 || j < 0 || j >= p_.ny) throw std::invalid_argument("zonal harmonic or row out of range");
  std::vector<double> row(p_.nx);
  for (int i = 0; i < p_.nx; i++) row[i] = h(i, j);
  return 2 * std::abs(harmonic(p_.nx, m, row, 0.5));
}
double Plane::zonal_crest(int m, int j) const {
  if (m < 1 || m >= p_.nx / 2 || j < 0 || j >= p_.ny) throw std::invalid_argument("zonal harmonic or row out of range");
  std::vector<double> row(p_.nx);
  for (int i = 0; i < p_.nx; i++) row[i] = h(i, j);
  double ph = std::arg(harmonic(p_.nx, m, row, 0.5));  // f ~ cos(2 pi m x / lx + ph): crest where the argument is 0 (mod 2 pi)
  double x = -ph / (2 * kPi * m) * p_.lx;
  double per = p_.lx / m;
  x = std::fmod(x, per);
  return x < 0 ? x + per : x;
}
double Plane::v_zonal_crest(int m, int j) const {
  if (m < 1 || m >= p_.nx / 2 || j < 1 || j >= p_.ny) throw std::invalid_argument("zonal harmonic or v row out of range");
  std::vector<double> row(p_.nx);
  for (int i = 0; i < p_.nx; i++) row[i] = v(i, j);
  double ph = std::arg(harmonic(p_.nx, m, row, 0.5));
  double x = -ph / (2 * kPi * m) * p_.lx;
  double per = p_.lx / m;
  x = std::fmod(x, per);
  return x < 0 ? x + per : x;
}

void Plane::set_h_from_grid(const std::vector<double> &g) { if (g.size() != h_.size()) throw std::invalid_argument("grid size mismatch"); h_ = g; }
void Plane::set_u_from_grid(const std::vector<double> &g) { if (g.size() != u_.size()) throw std::invalid_argument("grid size mismatch"); u_ = g; }
void Plane::set_v_from_grid(const std::vector<double> &g) {
  if (g.size() != v_.size()) throw std::invalid_argument("grid size mismatch");
  v_ = g;
  for (int i = 0; i < p_.nx; i++) { v_[i] = 0; v_[static_cast<size_t>(p_.ny) * p_.nx + i] = 0; }
}

}  // namespace lmweather
