#include "nano/helpers/MuonCorrection.h"

#include <correction.h>
#include <boost/math/special_functions/erf.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace nano {

namespace {

std::shared_ptr<correction::CorrectionSet> correction_set(const std::string &path) {
  static std::unordered_map<std::string, std::shared_ptr<correction::CorrectionSet>> cache;
  auto &entry = cache[path];
  if (!entry) {
    entry = std::shared_ptr<correction::CorrectionSet>(correction::CorrectionSet::from_file(path).release());
  }
  return entry;
}

double crystal_ball_cdf(double x, double mean, double sigma, double alpha, double n) {
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
  const auto delta = (x - mean) / sigma;

  if (delta < -abs_alpha) {
    const auto base = f - sigma * delta / g;
    return base > 0.0 ? norm_c1 / std::pow(base, n - 1.0) : norm_c1;
  }
  if (delta > abs_alpha) {
    const auto base = f + sigma * delta / g;
    return base > 0.0 ? norm_c1 * (c - std::pow(base, 1.0 - n)) : norm_c1 * c;
  }
  return norm_sigma * (d - sqrt_pi_over_two * std::erf(-delta / sqrt_two));
}

double crystal_ball_inverse(double u, double mean, double sigma, double alpha, double n) {
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
  const auto cdf_low = crystal_ball_cdf(mean - abs_alpha * sigma, mean, sigma, abs_alpha, n);
  const auto cdf_high = crystal_ball_cdf(mean + abs_alpha * sigma, mean, sigma, abs_alpha, n);
  u = std::clamp(u, std::numeric_limits<double>::epsilon(), 1.0 - std::numeric_limits<double>::epsilon());

  if (u < cdf_low) {
    return mean + g * (f - std::pow(norm_c1 / u, k));
  }
  if (u > cdf_high) {
    return mean - g * (f - std::pow(c - u / norm_c1, -k));
  }
  return mean - sqrt_two * sigma * boost::math::erf_inv((d - u / norm_sigma) / sqrt_pi_over_two);
}

float evaluate_sf(const correction::Correction::Ref &corr, const ObjectView &muon, const std::string &variation,
                  float minimum_pt) {
  if (muon.eta() < -2.4f || muon.eta() >= 2.4f || muon.pt() < minimum_pt) {
    return 1.0f;
  }
  return static_cast<float>(corr->evaluate({static_cast<double>(muon.eta()), static_cast<double>(muon.pt()), variation}));
}

}  // namespace

MuonCorrection::MuonCorrection(const ProducerConfig &config) {
  const auto key = config.era + "_NanoAOD" + config.nano_version;
  const auto it = config.muon_eras.find(key);
  if (it == config.muon_eras.end()) {
    throw std::runtime_error("Missing muon correction campaign: " + key);
  }
  const auto root = config.muon_payload_dir + "/" + it->second.payload_subdir + "/" + it->second.version;
  scale_smearing_ = correction_set(root + "/" + it->second.scale_smearing_file);
  scale_factors_ = correction_set(root + "/" + it->second.sf_file);
}

void MuonCorrection::correct(Event &event, std::vector<ObjectView> &muons) const {
  const auto suffix = event.is_mc() ? "mc" : "data";
  const auto a = scale_smearing_->at("a_" + std::string(suffix));
  const auto m = scale_smearing_->at("m_" + std::string(suffix));
  const auto random = scale_smearing_->at("RandomSmearing");
  const auto cb = scale_smearing_->at("cb_params");
  const auto poly = scale_smearing_->at("poly_params");
  const auto k_data = scale_smearing_->at("k_data");
  const auto k_mc = scale_smearing_->at("k_mc");

  for (auto &muon : muons) {
    const auto old_pt = muon.pt();
    const auto eta = static_cast<double>(muon.eta());
    const auto phi = static_cast<double>(muon.phi());
    const auto charge = static_cast<double>(muon.get<std::int32_t>("charge"));
    const auto a_value = a->evaluate({eta, phi, std::string("nom")});
    const auto m_value = m->evaluate({eta, phi, std::string("nom")});
    const auto denominator = m_value / old_pt + charge * a_value;
    auto corrected_pt = denominator != 0.0 ? 1.0 / denominator : static_cast<double>(old_pt);
    if (corrected_pt < 0.0) {
      corrected_pt = old_pt;
    }

    if (event.is_mc()) {
      const auto abs_eta = std::abs(eta);
      const auto layers = static_cast<double>(muon.get<std::int32_t>("nTrackerLayers"));
      const auto event_number = static_cast<int>(event.scalar<std::uint64_t>("event") & 0x7fffffffULL);
      const auto uniform = random->evaluate(
          {event_number, static_cast<int>(event.scalar<std::uint32_t>("luminosityBlock")), phi});
      const auto mean = cb->evaluate({abs_eta, layers, 0});
      const auto sigma = cb->evaluate({abs_eta, layers, 1});
      const auto n = cb->evaluate({abs_eta, layers, 2});
      const auto alpha = cb->evaluate({abs_eta, layers, 3});
      const auto random_cb = crystal_ball_inverse(uniform, mean, sigma, alpha, n);
      const auto p0 = poly->evaluate({abs_eta, layers, 0});
      const auto p1 = poly->evaluate({abs_eta, layers, 1});
      const auto p2 = poly->evaluate({abs_eta, layers, 2});
      const auto sigma_pt = std::max(0.0, p0 + p1 * corrected_pt + p2 * corrected_pt * corrected_pt);
      const auto kd = k_data->evaluate({abs_eta, std::string("nom")});
      const auto km = k_mc->evaluate({abs_eta, std::string("nom")});
      const auto extra_k = km < kd ? std::sqrt(kd * kd - km * km) : 0.0;
      corrected_pt *= 1.0 + extra_k * sigma_pt * random_cb;
    }

    const auto ratio = old_pt > 0.0f ? corrected_pt / old_pt : 1.0;
    if (!std::isfinite(corrected_pt) || ratio > 2.0 || ratio < 0.1 || (corrected_pt > 200.0 && old_pt <= 200.0f)) {
      corrected_pt = old_pt;
    }
    muon.set("pt", static_cast<float>(corrected_pt));
    muon.set("p4", ObjectView::LorentzVector(corrected_pt, muon.eta(), muon.phi(), muon.mass()));
  }
}

MuonSFResult MuonCorrection::scale_factor(const std::vector<ObjectView> &muons, const std::string &correction_key,
                                          float minimum_pt) const {
  const auto corr = scale_factors_->at(correction_key);
  float nominal = 1.0f;
  float stat_up = 1.0f;
  float stat_down = 1.0f;
  float syst_up = 1.0f;
  float syst_down = 1.0f;
  for (const auto &muon : muons) {
    const auto nominal_value = evaluate_sf(corr, muon, "nominal", minimum_pt);
    const auto stat_value = evaluate_sf(corr, muon, "stat", minimum_pt);
    const auto stat_delta = std::abs(stat_value);
    nominal *= nominal_value;
    stat_up *= nominal_value + stat_delta;
    stat_down *= nominal_value - stat_delta;
    syst_up *= evaluate_sf(corr, muon, "systup", minimum_pt);
    syst_down *= evaluate_sf(corr, muon, "systdown", minimum_pt);
  }
  return {nominal, stat_up, stat_down, syst_up, syst_down};
}

}  // namespace nano
