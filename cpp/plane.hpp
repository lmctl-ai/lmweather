// Shallow water on an f- or beta-plane: Arakawa C grid, periodic in x, free-slip walls in y, RK4 time stepping.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lmweather {

struct PlaneParams {
  int nx = 0, ny = 0;
  double lx = 0, ly = 0;
  double gravity = 9.81;
  double depth = 100;       // mean depth H (m)
  double f0 = 0, beta = 0;  // f = f0 + beta (y - ly/2)
  double viscosity = 0;     // Laplacian, m2/s
  double drag = 0;          // linear drag on the winds, 1/s
  bool linear = false;      // linearized about the rest state of depth H
};

class Plane {
 public:
  explicit Plane(const PlaneParams &p);
  const PlaneParams &params() const { return p_; }
  PlaneParams &params() { return p_; }
  double time() const { return t_; }
  double dx() const { return p_.lx / p_.nx; }
  double dy() const { return p_.ly / p_.ny; }

  // cell centers (x, y), u at west faces (x - dx/2 of the cell), v at south faces
  double x_center(int i) const { return (i + 0.5) * dx(); }
  double y_center(int j) const { return (j + 0.5) * dy(); }
  double &h(int i, int j) { return h_[static_cast<size_t>(j) * p_.nx + i]; }
  double &u(int i, int j) { return u_[static_cast<size_t>(j) * p_.nx + i]; }
  double &v(int i, int j) { return v_[static_cast<size_t>(j) * (p_.nx) + i]; }   // j = 0..ny, rows 0 and ny stay zero
  double h(int i, int j) const { return h_[static_cast<size_t>(j) * p_.nx + i]; }
  double u(int i, int j) const { return u_[static_cast<size_t>(j) * p_.nx + i]; }
  double v(int i, int j) const { return v_[static_cast<size_t>(j) * p_.nx + i]; }

  void reset();                                       // h = depth, u = v = 0, t = 0
  void perturb(double amplitude, uint64_t seed);      // seeded smooth random height noise of the given rms, zero mean
  void step(double dt);
  void run(double seconds, double dt);                // dt <= 0 picks a stable one
  double stable_dt(double safety = 0.5) const;

  double mean_depth() const;      // conserved exactly (up to rounding)
  double energy() const;          // J/m2 per unit density: mean of h|v|^2/2 + g h^2/2
  double max_speed() const;
  double potential_vorticity_anomaly_rms() const;  // rms of (zeta - f0 (h - H)/H): conserved by the linear equations
  double h_at(double x, double y) const;           // bilinear in the cell centers (periodic in x)
  double u_at(double x, double y) const;
  double v_at(double x, double y) const;
  double zonal_amplitude(int m, int j) const;      // amplitude of the m-th zonal harmonic of h on row j (2|c_m|)
  double harmonic_energy(int m) const;             // kinetic energy per unit mass in zonal harmonic m, mean over the channel: sum over m >= 1 plus the zonal-mean part is mean(|v|^2/2)
  double zonal_crest(int m, int j) const;          // longitude x (m) of a crest of that harmonic, in [0, lx/m)
  double v_zonal_crest(int m, int j) const;        // same for v (row j in 1..ny-1 on v points)
  double zeta_at_corner(int i, int j) const;
  void set_u_from_grid(const std::vector<double> &g);  // g has nx*ny values at the u points
  void set_h_from_grid(const std::vector<double> &g);
  void set_v_from_grid(const std::vector<double> &g);  // nx*(ny+1) values, rows 0 and ny are ignored (set to zero)

 private:
  PlaneParams p_;
  double t_ = 0;
  std::vector<double> h_, u_, v_;
  void tendency(const std::vector<double> &h, const std::vector<double> &u, const std::vector<double> &v, std::vector<double> &dh,
                std::vector<double> &du, std::vector<double> &dv) const;
  double coriolis(double y) const { return p_.f0 + p_.beta * (y - 0.5 * p_.ly); }
};

}  // namespace lmweather
