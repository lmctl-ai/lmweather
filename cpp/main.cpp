// lmweather: rotating shallow-water models (sphere by the spectral transform method, beta-plane by finite differences)
// behind an lmscript `_dsl_` schema. Scripting only: no user-facing commands besides --dsl and the schema/version queries.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "dslvm.hpp"
#include "expr.hpp"
#include "memory_budget.hpp"
#include "notices_embedded.hpp"
#include "plane.hpp"
#include "schema_embedded.hpp"
#include "sphere.hpp"

namespace {
using dslvm::Args;
using dslvm::Value;
using lmweather::Plane;
using lmweather::PlaneParams;
using lmweather::Sphere;
using lmweather::SphereParams;
const double kPi = 3.14159265358979323846;
const double kDeg = kPi / 180.0;

struct Wx {};
struct SphereObj {
  SphereParams params;
  std::unique_ptr<Sphere> model;
  Sphere &get() { return *model; }
};
struct PlaneObj {
  PlaneParams params;
  std::unique_ptr<Plane> model;
  bool started = false;  // fields were set: depth() would reset them
};

struct Host {
  std::vector<std::unique_ptr<SphereObj>> spheres;
  std::vector<std::unique_ptr<PlaneObj>> planes;
  template <class T> Value hold(std::vector<std::unique_ptr<T>> &arena, const char *type, std::unique_ptr<T> v) {
    if (arena.size() >= 100000) throw dslvm::Error(std::string(type) + " object limit exceeded");
    arena.push_back(std::move(v));
    return Value::object(type, arena.back().get());
  }
};

template <class F> Value translate(F &&fn) {
  try { return fn(); }
  catch (const dslvm::Error &) { throw; }
  catch (const std::exception &e) { throw dslvm::Error(e.what()); }
}

void method(dslvm::Registry &r, const char *type, const char *name, std::vector<std::string> ps, std::string ret, dslvm::Method fn) {
  const std::string who = std::string(type) + "." + name;
  const bool real = ret == "float";
  r.method(type, name, ps, ret, [who, ps, fn, real](void *self, Args &a) {
    for (size_t i = 0; i < ps.size(); ++i) {
      const auto &p = a[i];
      const bool ok = ps[i] == "float" ? p.kind == Value::FLOAT
                      : ps[i] == "int" ? p.kind == Value::INT
                      : ps[i] == "string" ? p.kind == Value::STR
                      : ps[i] == "bool" ? p.kind == Value::BOOL
                      : p.kind == Value::OBJ && p.obj && p.type == ps[i];
      if (!ok) throw dslvm::Error(who + " argument " + std::to_string(i + 1) + " must be " + ps[i]);
    }
    Value out = translate([&] { return fn(self, a); });
    // no diagnostic hands NaN/inf to a script (e.g. enstrophy with h <= 0, error norms against a zero reference)
    if (real && out.kind == Value::FLOAT && !std::isfinite(out.f)) throw dslvm::Error(who + ": the result is not finite");
    return out;
  });
}

template <class T> T &obj(void *p) { return *static_cast<T *>(p); }
std::string fmt(double x) { char b[48]; std::snprintf(b, sizeof b, "%.17g", x); return b; }
std::string g(double x) { char b[48]; std::snprintf(b, sizeof b, "%.10g", x); return b; }
void need(bool ok, const std::string &msg) { if (!ok) throw dslvm::Error(msg); }
void save(const std::string &path, const std::string &text, const char *who) {
  std::ofstream f(path, std::ios::binary);
  if (!f) throw dslvm::Error(std::string(who) + ": cannot write " + path);
  f << text;
  f.flush();
  if (!f) throw dslvm::Error(std::string(who) + ": cannot write " + path);
}
double finite(double x, const char *what) { need(std::isfinite(x), std::string(what) + " must be a finite number"); return x; }
// an int argument that is cast to int: reject values a cast would wrap instead of silently using another number
int narrow(int64_t v, const char *who) { need(v >= -1000000000 && v <= 1000000000, std::string(who) + ": integer argument out of range"); return static_cast<int>(v); }
double positive(double x, const std::string &msg) { need(std::isfinite(x) && x > 0, msg); return x; }
double latitude(double deg, const char *who) { need(std::fabs(deg) <= 90.0, std::string(who) + ": latitude must be within +-90 degrees"); return deg * kDeg; }

lmnum::Expr compile(const std::string &text, const std::vector<std::string> &vars, const std::map<std::string, double> &params) {
  return lmnum::Expr(text, vars, params);
}

SphereObj &built(void *p) {
  auto &s = obj<SphereObj>(p);
  return s;
}

void register_host(dslvm::Registry &r, Host &h) {
  r.context("wx", "Wx");
  // ---------------------------------------------------------------- Wx
  auto make_sphere = [&h](int64_t n, SphereParams sp) {
    need(n >= 4 && n <= 170, "Wx.sphere: truncation must be between 4 and 170");
    need(std::isfinite(sp.radius) && std::isfinite(sp.gravity) && std::isfinite(sp.omega) && sp.radius > 0 && sp.gravity > 0 && sp.omega >= 0, "Wx.planet: radius and gravity must be positive and omega not negative");
    auto o = std::make_unique<SphereObj>();
    o->params = sp;
    o->model = std::make_unique<Sphere>(static_cast<int>(n), sp);
    return h.hold(h.spheres, "Sphere", std::move(o));
  };
  method(r, "Wx", "sphere", {"int"}, "Sphere", [make_sphere](void *, Args &a) { return make_sphere(a[0].i, SphereParams{}); });
  method(r, "Wx", "planet", {"int", "float", "float", "float"}, "Sphere", [make_sphere](void *, Args &a) {
    SphereParams sp;
    sp.radius = a[1].f; sp.omega = a[2].f; sp.gravity = a[3].f;
    return make_sphere(a[0].i, sp);
  });
  method(r, "Wx", "plane", {"int", "int", "float", "float"}, "Plane", [&h](void *, Args &a) {
    need(a[0].i >= 4 && a[1].i >= 4 && a[0].i <= 4000 && a[1].i <= 4000 && a[0].i * a[1].i <= 8000000, "Wx.plane: need 4..4000 cells each way and at most 8 million cells");
    need(std::isfinite(a[2].f) && std::isfinite(a[3].f) && a[2].f > 0 && a[3].f > 0, "Wx.plane: domain sizes must be positive (m)");
    auto o = std::make_unique<PlaneObj>();
    o->params.nx = static_cast<int>(a[0].i); o->params.ny = static_cast<int>(a[1].i);
    o->params.lx = a[2].f; o->params.ly = a[3].f;
    o->model = std::make_unique<Plane>(o->params);
    return h.hold(h.planes, "Plane", std::move(o));
  });
  method(r, "Wx", "assert_close", {"float", "float", "float"}, "void", [](void *, Args &a) {
    if (!std::isfinite(a[0].f) || !std::isfinite(a[1].f) || !std::isfinite(a[2].f) || a[2].f < 0 || std::fabs(a[0].f - a[1].f) > a[2].f)
      throw dslvm::Error("assert_close failed: " + fmt(a[0].f) + " vs " + fmt(a[1].f) + " (tolerance " + fmt(a[2].f) + ")");
    return Value::none();
  });
  method(r, "Wx", "assert_true", {"bool"}, "void", [](void *, Args &a) {
    if (!a[0].b) throw dslvm::Error("assert_true failed");
    return Value::none();
  });
  method(r, "Wx", "print", {"string"}, "void", [](void *, Args &a) { std::cout << a[0].s << '\n'; return Value::none(); });
  method(r, "Wx", "save_text", {"string", "string"}, "void", [](void *, Args &a) { save(a[0].s, a[1].s, "Wx.save_text"); return Value::none(); });
  method(r, "Wx", "version", {}, "string", [](void *, Args &) { return Value::string("lmweather " LMWEATHER_VERSION); });

  // ---------------------------------------------------------------- Sphere
  auto S = [](void *p) -> Sphere & { return built(p).get(); };
  method(r, "Sphere", "tilt", {"float"}, "Sphere", [S](void *p, Args &a) {
    S(p).set_axis_tilt(finite(a[0].f, "tilt angle"));
    return Value::object("Sphere", p);
  });
  method(r, "Sphere", "diffusion", {"float"}, "Sphere", [S](void *p, Args &a) {
    need(std::isfinite(a[0].f) && a[0].f >= 0, "Sphere.diffusion: coefficient must be finite and not negative");
    S(p).set_diffusion(a[0].f);
    return Value::object("Sphere", p);
  });
  method(r, "Sphere", "fields", {"string", "string", "string"}, "Sphere", [S](void *p, Args &a) {
    Sphere &m = S(p);
    const auto &sp = m.params();
    std::map<std::string, double> par{{"a", sp.radius}, {"omega", sp.omega}, {"g", sp.gravity}};
    auto eh = compile(a[0].s, {"lon", "lat"}, par), eu = compile(a[1].s, {"lon", "lat"}, par), ev = compile(a[2].s, {"lon", "lat"}, par);
    size_t n = static_cast<size_t>(m.nlat()) * m.nlon();
    std::vector<double> hh(n), uu(n), vv(n);
    for (int j = 0; j < m.nlat(); j++)
      for (int k = 0; k < m.nlon(); k++) {
        double x[2] = {m.lon(k), m.lat(j)};
        size_t i = static_cast<size_t>(j) * m.nlon() + k;
        hh[i] = eh.eval(x); uu[i] = eu.eval(x); vv[i] = ev.eval(x);
        need(std::isfinite(hh[i] + uu[i] + vv[i]), "Sphere.fields: an expression is not finite on the grid");
      }
    m.set_state_grid(hh, uu, vv);
    return Value::object("Sphere", p);
  });
  method(r, "Sphere", "williamson2", {"float"}, "Sphere", [S](void *p, Args &a) {
    Sphere &m = S(p);
    const auto &sp = m.params();
    double al = finite(a[0].f, "angle"), u0 = 2 * kPi * sp.radius / (12 * 86400.0), h0 = 2.94e4 / sp.gravity;
    m.set_axis_tilt(al);
    size_t n = static_cast<size_t>(m.nlat()) * m.nlon();
    std::vector<double> hh(n), uu(n), vv(n);
    for (int j = 0; j < m.nlat(); j++)
      for (int k = 0; k < m.nlon(); k++) {
        double lat = m.lat(j), lon = m.lon(k);
        size_t i = static_cast<size_t>(j) * m.nlon() + k;
        uu[i] = u0 * (std::cos(lat) * std::cos(al) + std::sin(lat) * std::cos(lon) * std::sin(al));
        vv[i] = -u0 * std::sin(lon) * std::sin(al);
        double t = -std::cos(lon) * std::cos(lat) * std::sin(al) + std::sin(lat) * std::cos(al);
        hh[i] = h0 - (sp.radius * sp.omega * u0 + 0.5 * u0 * u0) * t * t / sp.gravity;
      }
    m.set_state_grid(hh, uu, vv);
    return Value::object("Sphere", p);
  });
  method(r, "Sphere", "williamson6", {}, "Sphere", [S](void *p, Args &) {
    Sphere &m = S(p);
    const auto &sp = m.params();
    const double w = 7.848e-6, K = 7.848e-6, R = 4, h0 = 8000, a = sp.radius, Om = sp.omega;
    m.set_axis_tilt(0);
    size_t n = static_cast<size_t>(m.nlat()) * m.nlon();
    std::vector<double> hh(n), uu(n), vv(n);
    for (int j = 0; j < m.nlat(); j++)
      for (int k = 0; k < m.nlon(); k++) {
        double ph = m.lat(j), la = m.lon(k), c = std::cos(ph), sn = std::sin(ph);
        size_t i = static_cast<size_t>(j) * m.nlon() + k;
        uu[i] = a * w * c + a * K * std::pow(c, R - 1) * (R * sn * sn - c * c) * std::cos(R * la);
        vv[i] = -a * K * R * std::pow(c, R - 1) * sn * std::sin(R * la);
        double A = w / 2 * (2 * Om + w) * c * c + K * K / 4 * std::pow(c, 2 * R) * ((R + 1) * c * c + (2 * R * R - R - 2) - 2 * R * R / (c * c));
        double B = 2 * (Om + w) * K / ((R + 1) * (R + 2)) * std::pow(c, R) * ((R * R + 2 * R + 2) - (R + 1) * (R + 1) * c * c);
        double C = K * K / 4 * std::pow(c, 2 * R) * ((R + 1) * c * c - (R + 2));
        hh[i] = h0 + a * a / sp.gravity * (A + B * std::cos(R * la) + C * std::cos(2 * R * la));
      }
    m.set_state_grid(hh, uu, vv);
    return Value::object("Sphere", p);
  });
  method(r, "Sphere", "orography", {"string"}, "Sphere", [S](void *p, Args &a) {
    Sphere &m = S(p);
    const auto &sp = m.params();
    std::map<std::string, double> par{{"a", sp.radius}, {"omega", sp.omega}, {"g", sp.gravity}};
    auto es = compile(a[0].s, {"lon", "lat"}, par);
    size_t n = static_cast<size_t>(m.nlat()) * m.nlon();
    std::vector<double> hs(n);
    for (int j = 0; j < m.nlat(); j++)
      for (int k = 0; k < m.nlon(); k++) {
        double x[2] = {m.lon(k), m.lat(j)};
        hs[static_cast<size_t>(j) * m.nlon() + k] = es.eval(x);
        need(std::isfinite(hs[static_cast<size_t>(j) * m.nlon() + k]), "Sphere.orography: the expression is not finite on the grid");
      }
    m.set_orography_grid(hs);
    return Value::object("Sphere", p);
  });
  method(r, "Sphere", "williamson5", {}, "Sphere", [S](void *p, Args &) {
    // Williamson et al. (1992) test case 5: zonal flow (u0 = 20 m/s, h0 = 5960 m) over an isolated conical mountain
    // (2000 m, radius pi/9, at 270 E 30 N); the flow starts in the balanced state of test case 2, the fluid thickness
    // is that free surface minus the mountain
    Sphere &m = S(p);
    const auto &sp = m.params();
    const double u0 = 20.0, h0 = 5960.0, hs0 = 2000.0, R = kPi / 9, lonc = 1.5 * kPi, latc = kPi / 6;
    m.set_axis_tilt(0);
    size_t n = static_cast<size_t>(m.nlat()) * m.nlon();
    std::vector<double> hh(n), uu(n), vv(n, 0.0), hs(n);
    for (int j = 0; j < m.nlat(); j++)
      for (int k = 0; k < m.nlon(); k++) {
        double lat = m.lat(j), lon = m.lon(k);
        size_t i = static_cast<size_t>(j) * m.nlon() + k;
        double rr = std::sqrt(std::min(R * R, (lon - lonc) * (lon - lonc) + (lat - latc) * (lat - latc)));
        hs[i] = hs0 * (1 - rr / R);
        uu[i] = u0 * std::cos(lat);
        hh[i] = h0 - (sp.radius * sp.omega * u0 + 0.5 * u0 * u0) * std::sin(lat) * std::sin(lat) / sp.gravity - hs[i];
      }
    m.set_orography_grid(hs);
    m.set_state_grid(hh, uu, vv);
    return Value::object("Sphere", p);
  });
  method(r, "Sphere", "tracer", {"string"}, "Sphere", [S](void *p, Args &a) {
    Sphere &m = S(p);
    const auto &sp = m.params();
    std::map<std::string, double> par{{"a", sp.radius}, {"omega", sp.omega}, {"g", sp.gravity}};
    auto eq = compile(a[0].s, {"lon", "lat"}, par);
    size_t n = static_cast<size_t>(m.nlat()) * m.nlon();
    std::vector<double> qq(n);
    for (int j = 0; j < m.nlat(); j++)
      for (int k = 0; k < m.nlon(); k++) {
        double x[2] = {m.lon(k), m.lat(j)};
        qq[static_cast<size_t>(j) * m.nlon() + k] = eq.eval(x);
        need(std::isfinite(qq[static_cast<size_t>(j) * m.nlon() + k]), "Sphere.tracer: the expression is not finite on the grid");
      }
    m.set_tracer_grid(qq);
    return Value::object("Sphere", p);
  });
  method(r, "Sphere", "advect_only", {}, "Sphere", [S](void *p, Args &) { S(p).set_advect_only(true); return Value::object("Sphere", p); });
  method(r, "Sphere", "williamson1", {"float"}, "Sphere", [S](void *p, Args &a) {
    // Williamson et al. (1992) test case 1: a cosine bell (h0 = 1000, radius a/3, centred at 270 E on the equator) carried once
    // around the globe in 12 days by solid-body rotation about an axis tilted by alpha; the winds are prescribed (advect-only)
    Sphere &m = S(p);
    const auto &sp = m.params();
    double al = finite(a[0].f, "angle"), u0 = 2 * kPi * sp.radius / (12 * 86400.0), R = sp.radius / 3, lonc = 1.5 * kPi, latc = 0;
    m.set_axis_tilt(al);
    size_t n = static_cast<size_t>(m.nlat()) * m.nlon();
    std::vector<double> hh(n, 1000.0), uu(n), vv(n), qq(n);
    for (int j = 0; j < m.nlat(); j++)
      for (int k = 0; k < m.nlon(); k++) {
        double lat = m.lat(j), lon = m.lon(k);
        size_t i = static_cast<size_t>(j) * m.nlon() + k;
        uu[i] = u0 * (std::cos(lat) * std::cos(al) + std::sin(lat) * std::cos(lon) * std::sin(al));
        vv[i] = -u0 * std::sin(lon) * std::sin(al);
        double c = std::sin(latc) * std::sin(lat) + std::cos(latc) * std::cos(lat) * std::cos(lon - lonc);
        double rr = sp.radius * std::acos(std::max(-1.0, std::min(1.0, c)));
        qq[i] = rr < R ? 500.0 * (1 + std::cos(kPi * rr / R)) : 0.0;
      }
    m.set_state_grid(hh, uu, vv);
    m.set_tracer_grid(qq);
    m.set_advect_only(true);
    return Value::object("Sphere", p);
  });
  method(r, "Sphere", "tracer_at", {"float", "float"}, "float", [S](void *p, Args &a) { return Value::real(S(p).tracer_at(latitude(a[0].f, "Sphere.tracer_at"), finite(a[1].f, "Sphere.tracer_at: longitude") * kDeg)); });
  method(r, "Sphere", "tracer_mean", {}, "float", [S](void *p, Args &) { return Value::real(S(p).tracer_mean()); });
  method(r, "Sphere", "tracer_mass", {}, "float", [S](void *p, Args &) { return Value::real(S(p).tracer_mass()); });
  method(r, "Sphere", "tracer_min", {}, "float", [S](void *p, Args &) { return Value::real(S(p).tracer_min()); });
  method(r, "Sphere", "tracer_max", {}, "float", [S](void *p, Args &) { return Value::real(S(p).tracer_max()); });
  method(r, "Sphere", "tracer_error_l2", {}, "float", [S](void *p, Args &) { return Value::real(S(p).tracer_error_l2()); });
  method(r, "Sphere", "tracer_error_linf", {}, "float", [S](void *p, Args &) { return Value::real(S(p).tracer_error_linf()); });
  method(r, "Sphere", "mode", {"int", "int", "float", "float"}, "Sphere", [S](void *p, Args &a) {
    positive(a[3].f, "Sphere.mode: mean depth must be positive");
    S(p).add_mode(narrow(a[0].i, "Sphere.mode"), narrow(a[1].i, "Sphere.mode"), finite(a[2].f, "amplitude"), a[3].f);
    return Value::object("Sphere", p);
  });
  method(r, "Sphere", "perturb", {"float", "int", "int"}, "Sphere", [S](void *p, Args &a) {
    need(a[1].i >= 0, "Sphere.perturb: seed must not be negative");
    S(p).perturb(finite(a[0].f, "amplitude"), static_cast<uint64_t>(a[1].i), narrow(a[2].i, "Sphere.perturb"));
    return Value::object("Sphere", p);
  });
  method(r, "Sphere", "mark_reference", {}, "Sphere", [S](void *p, Args &) { S(p).mark_reference(); return Value::object("Sphere", p); });
  method(r, "Sphere", "run", {"float"}, "Sphere", [S](void *p, Args &a) { S(p).run(finite(a[0].f, "seconds"), 0); return Value::object("Sphere", p); });
  method(r, "Sphere", "run_dt", {"float", "float"}, "Sphere", [S](void *p, Args &a) {
    positive(a[1].f, "Sphere.run_dt: dt must be positive");
    S(p).run(finite(a[0].f, "seconds"), a[1].f);
    return Value::object("Sphere", p);
  });
  method(r, "Sphere", "stable_dt", {}, "float", [S](void *p, Args &) { return Value::real(S(p).stable_dt()); });
  method(r, "Sphere", "time", {}, "float", [S](void *p, Args &) { return Value::real(S(p).time()); });
  method(r, "Sphere", "truncation", {}, "int", [S](void *p, Args &) { return Value::integer(S(p).truncation()); });
  method(r, "Sphere", "nlat", {}, "int", [S](void *p, Args &) { return Value::integer(S(p).nlat()); });
  method(r, "Sphere", "nlon", {}, "int", [S](void *p, Args &) { return Value::integer(S(p).nlon()); });
  method(r, "Sphere", "mean_height", {}, "float", [S](void *p, Args &) { return Value::real(S(p).mean_height()); });
  method(r, "Sphere", "energy", {}, "float", [S](void *p, Args &) { return Value::real(S(p).energy()); });
  method(r, "Sphere", "kinetic_energy", {}, "float", [S](void *p, Args &) { return Value::real(S(p).kinetic_energy()); });
  method(r, "Sphere", "potential_energy", {}, "float", [S](void *p, Args &) { return Value::real(S(p).potential_energy()); });
  method(r, "Sphere", "save_spectrum", {"string"}, "void", [S](void *p, Args &a) {
    std::vector<double> ke, pe;
    S(p).spectrum(ke, pe);
    std::string t;
    for (size_t n = 1; n < ke.size(); n++) t += std::to_string(n) + " " + g(ke[n]) + " " + g(pe[n]) + "\n";
    save(a[0].s, t, "Sphere.save_spectrum");
    return Value::none();
  });
  method(r, "Sphere", "enstrophy", {}, "float", [S](void *p, Args &) { return Value::real(S(p).potential_enstrophy()); });
  method(r, "Sphere", "max_wind", {}, "float", [S](void *p, Args &) { return Value::real(S(p).max_wind()); });
  method(r, "Sphere", "error_l2", {}, "float", [S](void *p, Args &) { return Value::real(S(p).height_error_l2()); });
  method(r, "Sphere", "error_linf", {}, "float", [S](void *p, Args &) { return Value::real(S(p).height_error_linf()); });
  method(r, "Sphere", "height_at", {"float", "float"}, "float", [S](void *p, Args &a) { return Value::real(S(p).height_at(latitude(a[0].f, "Sphere.height_at"), finite(a[1].f, "Sphere.height_at: longitude") * kDeg)); });
  method(r, "Sphere", "u_at", {"float", "float"}, "float", [S](void *p, Args &a) {
    need(std::fabs(a[0].f) < 90.0, "Sphere.u_at: latitude must be inside (-90, 90) degrees");
    return Value::real(S(p).wind_at(a[0].f * kDeg, finite(a[1].f, "Sphere.u_at: longitude") * kDeg, true));
  });
  method(r, "Sphere", "v_at", {"float", "float"}, "float", [S](void *p, Args &a) {
    need(std::fabs(a[0].f) < 90.0, "Sphere.v_at: latitude must be inside (-90, 90) degrees");
    return Value::real(S(p).wind_at(a[0].f * kDeg, finite(a[1].f, "Sphere.v_at: longitude") * kDeg, false));
  });
  method(r, "Sphere", "vorticity_at", {"float", "float"}, "float", [S](void *p, Args &a) { return Value::real(S(p).vorticity_at(latitude(a[0].f, "Sphere.vorticity_at"), finite(a[1].f, "Sphere.vorticity_at: longitude") * kDeg)); });
  method(r, "Sphere", "mode_coefficient", {"int", "int", "string"}, "float", [S](void *p, Args &a) {
    need(a[2].s == "re" || a[2].s == "im", "Sphere.mode_coefficient: part must be re or im");
    return Value::real(S(p).mode_coefficient(narrow(a[0].i, "Sphere.mode_coefficient"), narrow(a[1].i, "Sphere.mode_coefficient"), a[2].s == "im"));
  });
  method(r, "Sphere", "wave_crest", {"int", "float"}, "float", [S](void *p, Args &a) {
    return Value::real(S(p).wave_crest(narrow(a[0].i, "Sphere.wave_crest"), latitude(a[1].f, "Sphere.wave_crest")) / kDeg);
  });
  method(r, "Sphere", "save_csv", {"string"}, "void", [S](void *p, Args &a) {
    Sphere &m = S(p);
    std::vector<double> hh = m.grid_height(), uu = m.grid_u(), vv = m.grid_v(), zz = m.grid_vorticity();
    std::vector<double> qq = m.has_tracer() ? m.grid_tracer() : std::vector<double>(hh.size(), 0.0);
    const std::vector<double> &hs = m.grid_orography();
    std::string t;
    for (int j = 0; j < m.nlat(); j++) {
      for (int k = 0; k < m.nlon(); k++) {
        size_t i = static_cast<size_t>(j) * m.nlon() + k;
        t += g(m.lon(k) / kDeg) + " " + g(m.lat(j) / kDeg) + " " + g(hh[i]) + " " + g(uu[i]) + " " + g(vv[i]) + " " + g(zz[i]) + " " + g(qq[i]) + " " + g(hs[i]) + "\n";
      }
      t += "\n";  // blank line between latitude rows: gnuplot pm3d
    }
    save(a[0].s, t, "Sphere.save_csv");
    return Value::none();
  });
  method(r, "Sphere", "save_zonal_mean", {"string"}, "void", [S](void *p, Args &a) {
    Sphere &m = S(p);
    std::vector<double> hh = m.grid_height(), uu = m.grid_u();
    std::string t;
    for (int j = 0; j < m.nlat(); j++) {
      double sh = 0, su = 0;
      for (int k = 0; k < m.nlon(); k++) { sh += hh[static_cast<size_t>(j) * m.nlon() + k]; su += uu[static_cast<size_t>(j) * m.nlon() + k]; }
      t += g(m.lat(j) / kDeg) + " " + g(sh / m.nlon()) + " " + g(su / m.nlon()) + "\n";
    }
    save(a[0].s, t, "Sphere.save_zonal_mean");
    return Value::none();
  });

  // ---------------------------------------------------------------- Plane
  auto P = [](void *p) -> PlaneObj & { return obj<PlaneObj>(p); };
  auto sync = [](PlaneObj &o) { o.model->params() = o.params; };
  method(r, "Plane", "gravity", {"float"}, "Plane", [P, sync](void *p, Args &a) {
    positive(a[0].f, "Plane.gravity: must be positive");
    P(p).params.gravity = a[0].f; sync(P(p)); return Value::object("Plane", p);
  });
  method(r, "Plane", "depth", {"float"}, "Plane", [P, sync](void *p, Args &a) {
    positive(a[0].f, "Plane.depth: must be positive (m)");
    need(!P(p).started, "Plane.depth: set the depth before fields() or perturb()");
    P(p).params.depth = a[0].f; sync(P(p)); P(p).model->reset(); return Value::object("Plane", p);
  });
  method(r, "Plane", "coriolis", {"float", "float"}, "Plane", [P, sync](void *p, Args &a) {
    P(p).params.f0 = finite(a[0].f, "f0"); P(p).params.beta = finite(a[1].f, "beta"); sync(P(p)); return Value::object("Plane", p);
  });
  method(r, "Plane", "linear", {}, "Plane", [P, sync](void *p, Args &) { P(p).params.linear = true; sync(P(p)); return Value::object("Plane", p); });
  method(r, "Plane", "nonlinear", {}, "Plane", [P, sync](void *p, Args &) { P(p).params.linear = false; sync(P(p)); return Value::object("Plane", p); });
  method(r, "Plane", "viscosity", {"float"}, "Plane", [P, sync](void *p, Args &a) {
    need(std::isfinite(a[0].f) && a[0].f >= 0, "Plane.viscosity: must be finite and not negative"); P(p).params.viscosity = a[0].f; sync(P(p)); return Value::object("Plane", p);
  });
  method(r, "Plane", "drag", {"float"}, "Plane", [P, sync](void *p, Args &a) {
    need(std::isfinite(a[0].f) && a[0].f >= 0, "Plane.drag: must be finite and not negative"); P(p).params.drag = a[0].f; sync(P(p)); return Value::object("Plane", p);
  });
  method(r, "Plane", "fields", {"string", "string", "string"}, "Plane", [P](void *p, Args &a) {
    auto &o = P(p);
    Plane &m = *o.model;
    const auto &q = o.params;
    std::map<std::string, double> par{{"lx", q.lx}, {"ly", q.ly}, {"H", q.depth}, {"g", q.gravity}, {"f0", q.f0}, {"beta", q.beta}};
    auto eh = compile(a[0].s, {"x", "y"}, par), eu = compile(a[1].s, {"x", "y"}, par), ev = compile(a[2].s, {"x", "y"}, par);
    int nx = q.nx, ny = q.ny;
    std::vector<double> hh(static_cast<size_t>(nx) * ny), uu(hh.size()), vv(static_cast<size_t>(nx) * (ny + 1), 0.0);
    for (int j = 0; j < ny; j++)
      for (int i = 0; i < nx; i++) {
        double xc[2] = {m.x_center(i), m.y_center(j)}, xu[2] = {i * m.dx(), m.y_center(j)};
        hh[static_cast<size_t>(j) * nx + i] = q.depth + eh.eval(xc);
        uu[static_cast<size_t>(j) * nx + i] = eu.eval(xu);
      }
    for (int j = 1; j < ny; j++)
      for (int i = 0; i < nx; i++) { double xv[2] = {m.x_center(i), j * m.dy()}; vv[static_cast<size_t>(j) * nx + i] = ev.eval(xv); }
    for (double x : hh) need(std::isfinite(x), "Plane.fields: the height expression is not finite");
    for (double x : uu) need(std::isfinite(x), "Plane.fields: the u expression is not finite");
    for (double x : vv) need(std::isfinite(x), "Plane.fields: the v expression is not finite");
    m.set_h_from_grid(hh); m.set_u_from_grid(uu); m.set_v_from_grid(vv);
    o.started = true;
    return Value::object("Plane", p);
  });
  method(r, "Plane", "perturb", {"float", "int"}, "Plane", [P](void *p, Args &a) {
    need(a[1].i >= 0, "Plane.perturb: seed must not be negative");
    P(p).model->perturb(finite(a[0].f, "amplitude"), static_cast<uint64_t>(a[1].i));
    P(p).started = true;
    return Value::object("Plane", p);
  });
  method(r, "Plane", "run", {"float"}, "Plane", [P](void *p, Args &a) { P(p).model->run(finite(a[0].f, "seconds"), 0); P(p).started = true; return Value::object("Plane", p); });
  method(r, "Plane", "run_dt", {"float", "float"}, "Plane", [P](void *p, Args &a) {
    positive(a[1].f, "Plane.run_dt: dt must be positive");
    P(p).model->run(finite(a[0].f, "seconds"), a[1].f); P(p).started = true; return Value::object("Plane", p);
  });
  method(r, "Plane", "stable_dt", {}, "float", [P](void *p, Args &) { return Value::real(P(p).model->stable_dt()); });
  method(r, "Plane", "time", {}, "float", [P](void *p, Args &) { return Value::real(P(p).model->time()); });
  method(r, "Plane", "wave_speed", {}, "float", [P](void *p, Args &) { return Value::real(std::sqrt(P(p).params.gravity * P(p).params.depth)); });
  method(r, "Plane", "rossby_radius", {}, "float", [P](void *p, Args &) {
    need(P(p).params.f0 != 0, "Plane.rossby_radius: f0 is zero");
    return Value::real(std::sqrt(P(p).params.gravity * P(p).params.depth) / std::fabs(P(p).params.f0));
  });
  method(r, "Plane", "mean_depth", {}, "float", [P](void *p, Args &) { return Value::real(P(p).model->mean_depth()); });
  method(r, "Plane", "energy", {}, "float", [P](void *p, Args &) { return Value::real(P(p).model->energy()); });
  method(r, "Plane", "max_speed", {}, "float", [P](void *p, Args &) { return Value::real(P(p).model->max_speed()); });
  method(r, "Plane", "pv_anomaly_rms", {}, "float", [P](void *p, Args &) { return Value::real(P(p).model->potential_vorticity_anomaly_rms()); });
  auto inside = [P](void *p, double x, double y, const char *who) {
    need(x >= 0 && x <= P(p).params.lx && y >= 0 && y <= P(p).params.ly, std::string(who) + ": point outside the domain");
  };
  method(r, "Plane", "h_at", {"float", "float"}, "float", [P, inside](void *p, Args &a) { inside(p, a[0].f, a[1].f, "Plane.h_at"); return Value::real(P(p).model->h_at(a[0].f, a[1].f)); });
  method(r, "Plane", "u_at", {"float", "float"}, "float", [P, inside](void *p, Args &a) { inside(p, a[0].f, a[1].f, "Plane.u_at"); return Value::real(P(p).model->u_at(a[0].f, a[1].f)); });
  method(r, "Plane", "v_at", {"float", "float"}, "float", [P, inside](void *p, Args &a) { inside(p, a[0].f, a[1].f, "Plane.v_at"); return Value::real(P(p).model->v_at(a[0].f, a[1].f)); });
  method(r, "Plane", "zonal_amplitude", {"int", "int"}, "float", [P](void *p, Args &a) { return Value::real(P(p).model->zonal_amplitude(narrow(a[0].i, "Plane.zonal_amplitude"), narrow(a[1].i, "Plane.zonal_amplitude"))); });
  method(r, "Plane", "zonal_crest", {"int", "int"}, "float", [P](void *p, Args &a) { return Value::real(P(p).model->zonal_crest(narrow(a[0].i, "Plane.zonal_crest"), narrow(a[1].i, "Plane.zonal_crest"))); });
  method(r, "Plane", "v_zonal_crest", {"int", "int"}, "float", [P](void *p, Args &a) { return Value::real(P(p).model->v_zonal_crest(narrow(a[0].i, "Plane.v_zonal_crest"), narrow(a[1].i, "Plane.v_zonal_crest"))); });
  method(r, "Plane", "save_csv", {"string"}, "void", [P](void *p, Args &a) {
    Plane &m = *P(p).model;
    int nx = m.params().nx, ny = m.params().ny;
    std::string t;
    for (int j = 0; j < ny; j++) {
      for (int i = 0; i < nx; i++) {
        double uc = 0.5 * (m.u(i, j) + m.u((i + 1) % nx, j)), vc = 0.5 * (m.v(i, j) + m.v(i, j + 1));
        double z = 0.25 * (m.zeta_at_corner(i, j) + m.zeta_at_corner(i + 1, j) + m.zeta_at_corner(i, j + 1) + m.zeta_at_corner(i + 1, j + 1));
        t += g(m.x_center(i)) + " " + g(m.y_center(j)) + " " + g(m.h(i, j)) + " " + g(uc) + " " + g(vc) + " " + g(z) + "\n";
      }
      t += "\n";
    }
    save(a[0].s, t, "Plane.save_csv");
    return Value::none();
  });
  method(r, "Plane", "save_row", {"int", "string"}, "void", [P](void *p, Args &a) {
    Plane &m = *P(p).model;
    need(a[0].i >= 0 && a[0].i < m.params().ny, "Plane.save_row: row outside the grid");
    std::string t;
    for (int i = 0; i < m.params().nx; i++) t += g(m.x_center(i)) + " " + g(m.h(i, static_cast<int>(a[0].i))) + "\n";
    save(a[1].s, t, "Plane.save_row");
    return Value::none();
  });
  method(r, "Plane", "save_column", {"int", "string"}, "void", [P](void *p, Args &a) {
    Plane &m = *P(p).model;
    need(a[0].i >= 0 && a[0].i < m.params().nx, "Plane.save_column: column outside the grid");
    std::string t;
    for (int j = 0; j < m.params().ny; j++) t += g(m.y_center(j)) + " " + g(m.h(static_cast<int>(a[0].i), j)) + "\n";
    save(a[1].s, t, "Plane.save_column");
    return Value::none();
  });
}

const char *usage = "usage: lmweather --version | --notices | --dsl-schema [--json] | --dsl-signatures | [--dry-run] --dsl FILE [FILE ...]\n"
                    "environment: LMWEATHER_MAX_MEMORY_MB  total memory budget in MiB (default 4096); a program that needs more\n"
                    "             stops with a run error (exit 3) instead of allocating\n";
}  // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  Host h;
  dslvm::Registry reg;
  register_host(reg, h);
  std::string a1 = argc > 1 ? argv[1] : "";
  if (argc == 2 && a1 == "--version") { std::cout << "lmweather " LMWEATHER_VERSION "\n"; return 0; }
  if (argc == 2 && a1 == "--notices") {
    std::cout.write(reinterpret_cast<const char *>(lmweather::embedded_notices), (std::streamsize)lmweather::embedded_notices_size);
    return 0;
  }
  if (argc >= 2 && a1 == "--dsl-schema") {
    if (argc == 2) { for (const auto &s : reg.signatures()) std::cout << s << '\n'; return 0; }
    if (argc == 3 && std::string(argv[2]) == "--json") {
      std::cout.write(reinterpret_cast<const char *>(lmweather::embedded_schema), (std::streamsize)lmweather::embedded_schema_size);
      return 0;
    }
    std::cerr << usage;
    return 2;
  }
  if (argc == 2 && a1 == "--dsl-signatures") { for (const auto &s : reg.signatures()) std::cout << s << '\n'; return 0; }
  bool dry = false;
  int dsl_arg = 1;
  if (argc > 1 && a1 == "--dry-run") { dry = true; ++dsl_arg; }
  if (argc <= dsl_arg || std::string(argv[dsl_arg]) != "--dsl") { std::cerr << usage; return 2; }
  std::vector<std::string> files;
  for (int i = dsl_arg + 1; i < argc; i++) {
    if (std::string(argv[i]) == "--dry-run") {
      if (dry) { std::cerr << usage; return 2; }
      dry = true;
    } else if (std::string(argv[i]).empty() || std::string(argv[i]).rfind("--", 0) == 0) {
      std::cerr << usage;
      return 2;
    } else files.push_back(argv[i]);
  }
  if (files.empty()) { std::cerr << usage; return 2; }
  if (!membudget::configure("lmweather", "LMWEATHER_MAX_MEMORY_MB")) { std::cerr << usage; return 2; }
  const std::string schema(reinterpret_cast<const char *>(lmweather::embedded_schema), lmweather::embedded_schema_size);
  dslvm::Context ctx;
  Wx wx;
  ctx.set("wx", &wx);
  std::vector<dslvm::Program> programs;
  for (const auto &file : files)
    try { programs.push_back(dslvm::Program::load(dslvm::read_file(file), schema, reg)); }
    catch (const dslvm::Error &e) { return dslvm::report_error("load", e); }
    catch (const std::exception &e) { return dslvm::report_error("load", dslvm::Error(e.what())); }
  if (dry) { dslvm::report_ok(); return 0; }
  for (const auto &p : programs) {
    try { p.run(ctx, reg); }
    catch (const dslvm::Error &e) { return dslvm::report_error("run", e); }
    catch (const std::exception &e) { return dslvm::report_error("run", dslvm::Error(e.what())); }  // budget in the VM
    dslvm::report_ok();
  }
  return 0;
}
