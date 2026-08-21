#include "nano/helpers/MuonCorrection.h"

#include <correction.h>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

namespace nano {

namespace {

std::shared_ptr<correction::CorrectionSet>
correction_set(const std::string &path) {
  static std::unordered_map<std::string,
                            std::shared_ptr<correction::CorrectionSet>>
      cache;
  auto &entry = cache[path];
  if (!entry) {
    entry = std::shared_ptr<correction::CorrectionSet>(
        correction::CorrectionSet::from_file(path).release());
  }
  return entry;
}

float evaluate_sf(const correction::Correction::Ref &corr,
                  const ObjectView &muon, const std::string &variation,
                  float minimum_pt) {
  if (muon.eta() < -2.4f || muon.eta() >= 2.4f || muon.pt() < minimum_pt) {
    return 1.0f;
  }
  return static_cast<float>(
      corr->evaluate({static_cast<double>(muon.eta()),
                      static_cast<double>(muon.pt()), variation}));
}

std::size_t muon_variation_index(JmeVariation variation) {
  switch (variation) {
  case JmeVariation::MuonScaleUp:
    return 1U;
  case JmeVariation::MuonScaleDown:
    return 2U;
  case JmeVariation::MuonSmearUp:
    return 3U;
  case JmeVariation::MuonSmearDown:
    return 4U;
  default:
    return 0U;
  }
}

} // namespace

MuonCorrection::MuonCorrection(const ProducerConfig &config) {
  const auto key = config.era + "_NanoAOD" + config.nano_version;
  const auto it = config.muon_eras.find(key);
  if (it == config.muon_eras.end()) {
    throw std::runtime_error("Missing muon correction campaign: " + key);
  }
  const auto root = config.muon_payload_dir + "/" + it->second.payload_subdir +
                    "/" + it->second.version;
  const auto scale_smearing_file = root + "/" + it->second.scale_smearing_file;
  mc_calculator_ = MuonVariationsCalculator::create(
      scale_smearing_file, true, true, config.muon_smearing_file,
      config.muon_smearing_tool);
  data_calculator_ = MuonVariationsCalculator::create(scale_smearing_file,
                                                      false, false, "", "");
  scale_factors_ = correction_set(root + "/" + it->second.sf_file);
}

MuonVariationsCalculator::result_t
MuonCorrection::produce(Event &event,
                        const std::vector<ObjectView> &muons) const {
  MuonVariationsCalculator::p4compv_t pt;
  MuonVariationsCalculator::p4compv_t eta;
  MuonVariationsCalculator::p4compv_t phi;
  MuonVariationsCalculator::p4compv_int charge;
  MuonVariationsCalculator::p4compv_int layers;
  pt.reserve(muons.size());
  eta.reserve(muons.size());
  phi.reserve(muons.size());
  charge.reserve(muons.size());
  layers.reserve(muons.size());
  for (const auto &muon : muons) {
    pt.push_back(muon.pt());
    eta.push_back(muon.eta());
    phi.push_back(muon.phi());
    charge.push_back(muon.get<std::int32_t>("charge"));
    layers.push_back(muon.get<std::int32_t>("nTrackerLayers"));
  }

  const auto seed =
      static_cast<int>(event.scalar<std::uint64_t>("event") & 0x7fffffffULL);
  return event.is_mc()
             ? mc_calculator_.produce(pt, eta, phi, charge, layers, seed)
             : data_calculator_.produce(pt, eta, phi, charge, layers, seed);
}

void MuonCorrection::apply(const MuonVariationsCalculator::result_t &result,
                           JmeVariation variation,
                           std::vector<ObjectView> &muons) const {
  const auto index = muon_variation_index(variation);
  if (index >= result.size()) {
    throw std::runtime_error("Requested muon variation is unavailable");
  }
  const auto &corrected_pt = result.pt(index);
  if (corrected_pt.size() != muons.size()) {
    throw std::runtime_error("Muon correction output size mismatch");
  }
  for (std::size_t i = 0; i < muons.size(); ++i) {
    muons[i].set("pt", corrected_pt[i]);
    muons[i].set("p4",
                 ObjectView::LorentzVector(corrected_pt[i], muons[i].eta(),
                                           muons[i].phi(), muons[i].mass()));
  }
}

MuonSFResult MuonCorrection::scale_factor(const std::vector<ObjectView> &muons,
                                          const std::string &correction_key,
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

} // namespace nano
