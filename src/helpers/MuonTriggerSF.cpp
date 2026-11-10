#include "nano/helpers/MuonTriggerSF.h"
#include <correction.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <yaml-cpp/yaml.h>
namespace nano {
/*
 * Muon trigger SF usage
 * - Use the era-specific high-pt single-muon trigger OR from muon_Z, with the
 *   CutBasedIdGlobalHighPt_and_TkIsoLoose denominator. This helper does not
 *   impose ID/isolation cuts; callers supply the selected muons.
 * - Eligible muons have pt >= minimum_pt (default 52 GeV). Run 2 requires
 *   |eta| < 2.4 and looks up absolute eta; Run 3 uses signed eta in [-2.4, 2.4).
 * - The event SF is [1 - product(1 - efficiency_DATA)] divided by
 *   [1 - product(1 - efficiency_MC)], rather than a product of single-muon SFs.
 *   Prefer official DATA/MC efficiencies (source=1); otherwise require a
 *   complete, era/key-matched derived MC payload and use DATA=SF*MC (source=2).
 * - Stat and syst vary the independent single-muon SF components coherently;
 *   effstat varies derived MC efficiencies and takes the event-SF envelope.
 *   Every output is a full alternate event weight for downstream MC weighting.
 * - No eligible muon returns unity/source=0. Missing/mismatched efficiencies,
 *   invalid bins, nonphysical inputs or zero event MC efficiency throw.
 */

std::string MuonTriggerSF::correction_name(const std::string &era) {
  std::string paths;
  if (era == "2016APV" || era == "2016") paths = "Mu50_or_TkMu50";
  else if (era == "2017" || era == "2018") paths = "Mu50_or_OldMu100_or_TkMu100";
  else if (era == "2022" || era == "2022EE" || era == "2023" || era == "2023BPix" ||
           era == "2024" || era == "2025") paths = "Mu50_or_CascadeMu100_or_HighPtTkMu100";
  else throw std::runtime_error("Unsupported muon HLT era: " + era);
  return "NUM_" + paths + "_DEN_CutBasedIdGlobalHighPt_and_TkIsoLoose";
}
MuonTriggerSF::MuonTriggerSF(const std::string &era, const std::string &file, const std::string &aux)
    : era_(era), key_(correction_name(era)), absolute_eta_(era.rfind("201", 0) == 0) {
  auto cs = correction::CorrectionSet::from_file(file);
  sf_ = cs->at(key_);
  // Check the actual category interface; never turn unrelated payload errors into fallbacks.
  sf_->evaluate({0.5, 100., std::string("nominal")});
  try {
    sf_->evaluate({0.5, 100., std::string("nominal_DATAeff")});
    sf_->evaluate({0.5, 100., std::string("nominal_MCeff")});
    official_efficiencies_ = true;
  } catch (const std::exception &e) {
    if (std::string(e.what()).find("Index not available in Category") == std::string::npos) throw;
  }
  if (!official_efficiencies_) {
    if (aux.empty()) throw std::runtime_error("Missing derived muon HLT efficiency for " + era);
    auto eff = correction::CorrectionSet::from_file(aux);
    const auto name = "muon_hlt_" + era + "_mc_efficiency";
    efficiency_ = eff->at(name);
    efficiency_valid_ = eff->at(name + "_valid");
    const auto metadata = YAML::Load(efficiency_->description());
    if (metadata["era"].as<std::string>() != era || metadata["sf_key"].as<std::string>() != key_ ||
        !metadata["complete"].as<bool>())
      throw std::runtime_error("Incomplete or mismatched muon HLT efficiency: " + aux);
  }
}
MuonSFResult MuonTriggerSF::evaluate(const std::vector<ObjectView> &muons, float minimum_pt,
                                     const std::string &key) const {
  if (!key.empty() && key != key_) throw std::runtime_error("Muon HLT key does not match era " + era_);
  std::array<double, 5> data_fail{1.,1.,1.,1.,1.};
  std::array<double, 3> mc_fail{1.,1.,1.}, eff_data_fail{1.,1.,1.};
  bool any = false;
  const auto probability = [](double p) {
    if (!std::isfinite(p) || p < 0. || p > 1.)
      throw std::runtime_error("Nonphysical muon HLT efficiency");
    return p;
  };
  for (const auto &mu : muons) {
    if (!std::isfinite(mu.eta()) || !std::isfinite(mu.pt()))
      throw std::runtime_error("Nonfinite muon HLT coordinate");
    if ((absolute_eta_ && std::abs(mu.eta()) >= 2.4f) || mu.eta() < -2.4f || mu.eta() >= 2.4f || mu.pt() < minimum_pt) continue;
    any = true;
    const double eta = absolute_eta_ ? std::abs(mu.eta()) : mu.eta();
    const auto sf = [&](const char *v) { return sf_->evaluate({eta, double(mu.pt()), std::string(v)}); };
    const auto eff = [&](const char *v) { return efficiency_->evaluate({eta, double(mu.pt()), std::string(v)}); };
    if (!official_efficiencies_ && efficiency_valid_->evaluate({eta,double(mu.pt())}) < .5)
      throw std::runtime_error("Invalid derived muon HLT efficiency bin for " + era_);
    const double mc = probability(official_efficiencies_ ? sf("nominal_MCeff") : eff("nominal"));
    const double nominal_sf = sf("nominal");
    const double data = probability(official_efficiencies_ ? sf("nominal_DATAeff") : nominal_sf * mc);
    const double stat = std::abs(sf("stat")), syst = std::abs(sf("syst"));
    if (!std::isfinite(nominal_sf) || nominal_sf < 0 || !std::isfinite(stat) || !std::isfinite(syst))
      throw std::runtime_error("Invalid muon HLT SF");
    const std::array<double,5> shifts{0.,stat,-stat,syst,-syst};
    for (unsigned i=0;i<5;++i) data_fail[i] *= 1. - (i == 0 ? data : std::clamp(data + mc*shifts[i],0.,1.));
    for (unsigned i=0;i<3;++i) {
      const double m = official_efficiencies_ || i==0 ? mc : probability(eff(i==1 ? "stat_up" : "stat_down"));
      const double d = official_efficiencies_ ? data : std::clamp(nominal_sf*m, 0., 1.);
      mc_fail[i] *= 1.-m;
      eff_data_fail[i] *= 1.-d;
    }
  }
  MuonSFResult result;
  if (!any) return result;
  for (const auto fail : mc_fail) if (fail >= 1.) throw std::runtime_error("Zero muon HLT OR MC efficiency");
  const auto ratio = [&](double fail) { return float((1.-fail)/(1.-mc_fail[0])); };
  result.nominal=ratio(data_fail[0]); result.stat_up=ratio(data_fail[1]); result.stat_down=ratio(data_fail[2]);
  result.syst_up=ratio(data_fail[3]); result.syst_down=ratio(data_fail[4]);
  const float a=(1.-eff_data_fail[1])/(1.-mc_fail[1]), b=(1.-eff_data_fail[2])/(1.-mc_fail[2]);
  result.effstat_up=std::max({result.nominal,a,b}); result.effstat_down=std::min({result.nominal,a,b});
  result.source=official_efficiencies_ ? 1 : 2;
  return result;
}
}
