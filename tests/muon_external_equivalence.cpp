#include "MuonVariationsCalculator.h"

#include <boost/math/special_functions/erf.hpp>
#include <correction.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct CrystalBall {
  double mean;
  double sigma;
  double alpha;
  double n;

  double inverse(double u) const {
    constexpr double pi = 3.14159265358979323846;
    const auto abs_alpha = std::abs(alpha);
    const auto sqrt_pi_over_two = std::sqrt(pi / 2.0);
    const auto sqrt_two = std::sqrt(2.0);
    const auto exponential = std::exp(-0.5 * abs_alpha * abs_alpha);
    const auto c1 = n / abs_alpha / (n - 1.0) * exponential;
    const auto d1 = 2.0 * sqrt_pi_over_two * std::erf(abs_alpha / sqrt_two);
    const auto norm = 1.0 / sigma / (d1 + 2.0 * c1);
    const auto norm_sigma = norm * sigma;
    const auto norm_c1 = norm_sigma * c1;
    const auto f = 1.0 - abs_alpha * abs_alpha / n;
    const auto g = sigma * n / abs_alpha;
    const auto c = (d1 + 2.0 * c1) / c1;
    const auto d = (d1 + 2.0 * c1) / 2.0;
    const auto k = 1.0 / (n - 1.0);
    const auto cdf = [&](double x) {
      const auto delta = (x - mean) / sigma;
      if (delta < -abs_alpha) {
        return norm_c1 / std::pow(f - sigma * delta / g, n - 1.0);
      }
      if (delta > abs_alpha) {
        return norm_c1 * (c - std::pow(f + sigma * delta / g, 1.0 - n));
      }
      return norm_sigma * (d - sqrt_pi_over_two * std::erf(-delta / sqrt_two));
    };
    if (u < cdf(mean - abs_alpha * sigma)) {
      return mean + g * (f - std::pow(norm_c1 / u, k));
    }
    if (u > cdf(mean + abs_alpha * sigma)) {
      return mean - g * (f - std::pow(c - u / norm_c1, -k));
    }
    return mean -
           sqrt_two * sigma *
               boost::math::erf_inv((d - u / norm_sigma) / sqrt_pi_over_two);
  }
};

double extra_k(double data, double mc) {
  return data * data > mc * mc ? std::sqrt(data * data - mc * mc) : 0.0;
}

std::vector<std::vector<double>>
legacy(const std::string &muon_file, const std::string &random_file,
       const MuonVariationsCalculator::p4compv_t &pts,
       const MuonVariationsCalculator::p4compv_t &etas,
       const MuonVariationsCalculator::p4compv_t &phis,
       const MuonVariationsCalculator::p4compv_int &charges,
       const MuonVariationsCalculator::p4compv_int &layers, int seed,
       bool is_mc) {
  auto corrections = correction::CorrectionSet::from_file(muon_file);
  auto randoms = correction::CorrectionSet::from_file(random_file);
  const auto suffix = is_mc ? "mc" : "data";
  const auto a = corrections->at("a_" + std::string(suffix));
  const auto m = corrections->at("m_" + std::string(suffix));
  const auto cb = corrections->at("cb_params");
  const auto poly = corrections->at("poly_params");
  const auto kd = corrections->at("k_data");
  const auto km = corrections->at("k_mc");
  const auto random = randoms->at("stdflat");
  std::vector<std::vector<double>> out(
      is_mc ? 5U : 1U, std::vector<double>(pts.begin(), pts.end()));

  for (std::size_t i = 0; i < pts.size(); ++i) {
    const auto scale =
        1.0 /
        (m->evaluate({etas[i], phis[i], std::string("nom")}) / pts[i] +
         charges[i] * a->evaluate({etas[i], phis[i], std::string("nom")})) /
        pts[i];
    const auto scaled_pt = pts[i] * scale;
    if (!is_mc) {
      out[0][i] = scaled_pt;
      continue;
    }

    const auto abs_eta = std::abs(etas[i]);
    CrystalBall crystal_ball{
        cb->evaluate({abs_eta, static_cast<double>(layers[i]), 0}),
        cb->evaluate({abs_eta, static_cast<double>(layers[i]), 1}),
        cb->evaluate({abs_eta, static_cast<double>(layers[i]), 3}),
        cb->evaluate({abs_eta, static_cast<double>(layers[i]), 2}),
    };
    const auto uniform = random->evaluate({pts[i], etas[i], phis[i], seed});
    const auto draw = crystal_ball.inverse(uniform);
    const auto standard_deviation = [&](double pt) {
      return std::max(
          0.0,
          poly->evaluate({abs_eta, static_cast<double>(layers[i]), 0}) +
              poly->evaluate({abs_eta, static_cast<double>(layers[i]), 1}) *
                  pt +
              poly->evaluate({abs_eta, static_cast<double>(layers[i]), 2}) *
                  pt * pt);
    };
    const auto nominal_k = extra_k(kd->evaluate({abs_eta, std::string("nom")}),
                                   km->evaluate({abs_eta, std::string("nom")}));
    const auto mc_stat = km->evaluate({abs_eta, std::string("stat")});
    const auto smear_up_k =
        extra_k(kd->evaluate({abs_eta, std::string("nom")}),
                km->evaluate({abs_eta, std::string("nom")}) + mc_stat);
    const auto smear_down_k =
        extra_k(kd->evaluate({abs_eta, std::string("nom")}),
                km->evaluate({abs_eta, std::string("nom")}) - mc_stat);
    const auto a_stat = a->evaluate({etas[i], phis[i], std::string("stat")});
    const auto m_stat = m->evaluate({etas[i], phis[i], std::string("stat")});
    const auto rho_stat =
        m->evaluate({etas[i], phis[i], std::string("rho_stat")});
    const auto scale_uncertainty =
        scaled_pt * scaled_pt *
        std::sqrt(m_stat * m_stat / (scaled_pt * scaled_pt) + a_stat * a_stat +
                  2.0 * charges[i] * rho_stat * m_stat * a_stat / scaled_pt);
    out[0][i] =
        scaled_pt * (1.0 + nominal_k * standard_deviation(scaled_pt) * draw);
    out[1][i] =
        (scaled_pt + scale_uncertainty) *
        (1.0 +
         nominal_k * standard_deviation(scaled_pt + scale_uncertainty) * draw);
    out[2][i] =
        (scaled_pt - scale_uncertainty) *
        (1.0 +
         nominal_k * standard_deviation(scaled_pt - scale_uncertainty) * draw);
    out[3][i] =
        scaled_pt * (1.0 + smear_up_k * standard_deviation(scaled_pt) * draw);
    out[4][i] =
        scaled_pt * (1.0 + smear_down_k * standard_deviation(scaled_pt) * draw);
  }
  return out;
}

void compare(const MuonVariationsCalculator::result_t &actual,
             const std::vector<std::vector<double>> &expected) {
  if (actual.size() != expected.size()) {
    throw std::runtime_error("Variation count mismatch");
  }
  for (std::size_t variation = 0; variation < expected.size(); ++variation) {
    for (std::size_t muon = 0; muon < expected[variation].size(); ++muon) {
      const auto observed = static_cast<double>(actual.pt(variation)[muon]);
      const auto reference = expected[variation][muon];
      const auto tolerance = std::max(1e-5, 1e-6 * std::abs(reference));
      if (std::abs(observed - reference) > tolerance) {
        throw std::runtime_error(
            "Muon output mismatch at variation " + std::to_string(variation) +
            ", muon " + std::to_string(muon) + ": " + std::to_string(observed) +
            " != " + std::to_string(reference));
      }
    }
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Usage: muon_external_equivalence <muon.json.gz> "
                 "<stdflat.json.gz>\n";
    return 2;
  }
  try {
    const MuonVariationsCalculator::p4compv_t pt{32.0f, 75.0f, 240.0f, 900.0f};
    const MuonVariationsCalculator::p4compv_t eta{-2.1f, -0.4f, 0.8f, 2.2f};
    const MuonVariationsCalculator::p4compv_t phi{-2.7f, -0.2f, 1.3f, 2.8f};
    const MuonVariationsCalculator::p4compv_int charge{-1, 1, -1, 1};
    const MuonVariationsCalculator::p4compv_int layers{9, 12, 15, 18};
    constexpr int seed = 123456789;

    auto mc = MuonVariationsCalculator::create(argv[1], true, true, argv[2],
                                               "stdflat");
    const std::vector<std::string> expected_labels{
        "nominal", "mesScaleup", "mesScaledown", "mesSmearup", "mesSmeardown"};
    if (mc.available() != expected_labels) {
      throw std::runtime_error("Unexpected external variation labels");
    }
    compare(mc.produce(pt, eta, phi, charge, layers, seed),
            legacy(argv[1], argv[2], pt, eta, phi, charge, layers, seed, true));

    auto data = MuonVariationsCalculator::create(argv[1], false, false, "", "");
    compare(
        data.produce(pt, eta, phi, charge, layers, seed),
        legacy(argv[1], argv[2], pt, eta, phi, charge, layers, seed, false));
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
