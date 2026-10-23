#include "nano/producers/HeavyFlavZmmSampleProducer.h"

#include "nano/core/Collection.h"
#include "nano/core/Helpers.h"

#include <algorithm>
#include <cmath>

namespace nano {

/*
 * Channel summary: zmm
 *
 * Purpose
 * - Select boosted Z->mumu events for a clean recoil-jet control sample.
 *
 * Event selection implemented in this producer
 * - Apply the configured muon scale correction and MC resolution smearing.
 * - Require exactly two muons with pt > 30 GeV, |eta| < 2.4, and
 *   `highPtId != 0` (tracker or global high-pt ID).
 * - Veto additional loose-ID muons with pt > 15 GeV and |eta| < 2.4.
 * - Require the selected muons to have opposite charges and leading pt >= 60 GeV.
 * - Require dimuon pt >= 400 GeV and 70 <= mass <= 110 GeV.
 * - Build loose-lepton collections for jet cleaning through shared
 *   base-producer helpers, using nominal corrected muon momenta.
 * - Compute AK4/AK8/SubJet JME corrections once, then apply the requested
 *   variation before selecting the cleaned AK8 jets and corrected MET.
 * - Require at least one corrected AK8 jet separated from both selected muons
 *   by DeltaR > 0.8.
 * - Keep only the leading separated AK8 jet for output.
 * - Store the configured muon-trigger OR decision without applying a trigger
 *   veto in this producer.
 */

HeavyFlavZmmSampleProducer::HeavyFlavZmmSampleProducer(ProducerConfig config)
    : HeavyFlavBaseProducer([&config] {
        config.channel = "zmm";
        return config;
      }()),
      nlo_ew_weight_producer_(config_),
      muon_corrector_(config_) {}

void HeavyFlavZmmSampleProducer::begin_file() {
  HeavyFlavBaseProducer::begin_file();
  nlo_ew_weight_producer_.begin_file(out_);
  out_.branch("passMuTrig", false);
  out_.branch("leptonicZ_pt", 0.0f);
  out_.branch("leptonicZ_mass", 0.0f);
  for (int index = 0; index < 2; ++index) {
    const auto prefix = "muon" + std::to_string(index) + "_";
    out_.branch(prefix + "pt", 0.0f);
    out_.branch(prefix + "eta", 0.0f);
    out_.branch(prefix + "phi", 0.0f);
    out_.branch(prefix + "mass", 0.0f);
    out_.branch(prefix + "miniIso", 0.0f);
  }
  for (const std::string name : {"muonHLTSF", "muonIDSF"}) {
    out_.branch(name, 1.0f);
    out_.branch(name + "_stat_up", 1.0f);
    out_.branch(name + "_stat_down", 1.0f);
    out_.branch(name + "_syst_up", 1.0f);
    out_.branch(name + "_syst_down", 1.0f);
  }
}

namespace {

JmeVariation muon_selection_variation(JmeVariation variation) {
  if (variation == JmeVariation::MuonScaleUp || variation == JmeVariation::MuonScaleDown ||
      variation == JmeVariation::MuonSmearUp || variation == JmeVariation::MuonSmearDown) {
    return variation;
  }
  return JmeVariation::Nominal;
}

}  // namespace

// Compute muon momentum corrections once and cache the dimuon selection for
// each requested muon state.
bool HeavyFlavZmmSampleProducer::analyze_common(Event &event) {
  auto muons = event.collection("Muon").objects();
  // Muon scale/smear variations leave eta and ID unchanged. Reject impossible
  // pairs before evaluating any momentum corrections.
  const auto high_pt_count = std::count_if(muons.begin(), muons.end(), [](const auto &muon) {
    return std::abs(muon.eta()) < 2.4f && muon.template get<std::int32_t>("highPtId") != 0;
  });
  if (high_pt_count < 2) {
    return false;
  }
  // Compute corrected muon pT once, including MC resolution smearing and the
  // scale/smear shifts when requested. Cache all states for later selection.
  auto variations = muon_corrector_.compute(event, muons);
  event.set("muon_variations", std::move(variations));

  bool passes_any_variation = false;
  for (const auto variation : config_.requested_variations) {
    const auto muon_variation = muon_selection_variation(variation);
    const auto indices_key = "muon_indices_" + std::string(variation_name(muon_variation));
    if (!event.has(indices_key)) {
      // Select and cache the dimuon pair once per distinct muon state.
      select_muons(event, muon_variation);
    }
    passes_any_variation = !event.get<std::vector<std::size_t>>(indices_key).empty() || passes_any_variation;
  }
  if (!passes_any_variation) {
    return false;
  }
  // Common lepton cleaning uses nominal kinematics, independently of the
  // requested variation order. ObjectView instances share these mutable values.
  muon_corrector_.apply(event.get<MuonVariationsCalculator::result_t>("muon_variations"), JmeVariation::Nominal, muons);
  prepare_common_objects(event);
  return true;
}

// Apply the cached muon pT corrections for this state.
// Cache the selected muons' indices in the original Muon collection, ordered by
// descending pT, and their combined four-vector under keys specific to this state.
void HeavyFlavZmmSampleProducer::select_muons(Event &event, JmeVariation variation) {
  const auto suffix = std::string(variation_name(variation));
  event.set("muon_indices_" + suffix, std::vector<std::size_t>{});
  auto muons = event.collection("Muon").objects();
  muon_corrector_.apply(event.get<MuonVariationsCalculator::result_t>("muon_variations"), variation, muons);
  std::vector<ObjectView> selected;
  // Require exactly two tracker/global high-pt-ID muons with pT > 30 GeV and |eta| < 2.4.
  for (auto &muon : muons) {
    if (muon.pt() > 30.0f && std::abs(muon.eta()) < 2.4f &&
        muon.get<std::int32_t>("highPtId") != 0) {
      selected.push_back(muon);
    }
  }
  if (selected.size() != 2U) {
    return;
  }
  // Veto any additional loose-ID muon with pT > 15 GeV and |eta| < 2.4.
  for (const auto &muon : muons) {
    const auto is_selected = std::any_of(
        selected.begin(), selected.end(), [&muon](const auto &candidate) {
          return candidate.index() == muon.index();
        });
    if (!is_selected && muon.pt() > 15.0f && std::abs(muon.eta()) < 2.4f &&
        muon.get<bool>("looseId")) {
      return;
    }
  }
  // Order by pT; require leading/subleading pT >= 60/30 GeV and opposite charges.
  std::sort(selected.begin(), selected.end(), [](const auto &a, const auto &b) { return a.pt() > b.pt(); });
  if (selected[0].pt() < 60.0f || selected[1].pt() < 30.0f ||
      selected[0].get<std::int32_t>("charge") * selected[1].get<std::int32_t>("charge") > 0) {
    return;
  }

  // Require dimuon pT >= 400 GeV and mass in [70, 110] GeV.
  const auto z = selected[0].p4() + selected[1].p4();
  if (z.Pt() < 400.0 || z.M() < 70.0 || z.M() > 110.0) {
    return;
  }
  event.set("muon_indices_" + suffix, std::vector<std::size_t>{selected[0].index(), selected[1].index()});
  event.set("leptonicZ_" + suffix, z);
}

// Restore the cached muon state, apply the requested JME variation, and select
// the leading recoil jet. Fill accepted events once per requested variation;
// muon variations use nominal JME, while JME variations use nominal muons.
bool HeavyFlavZmmSampleProducer::analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) {
  const auto muon_variation = muon_selection_variation(variation);
  const auto suffix = std::string(variation_name(muon_variation));
  const auto &indices = event.get<std::vector<std::size_t>>("muon_indices_" + suffix);
  if (indices.empty()) {
    return false;
  }
  auto all_muons = event.collection("Muon").objects();
  muon_corrector_.apply(event.get<MuonVariationsCalculator::result_t>("muon_variations"), muon_variation, all_muons);
  std::vector<ObjectView> selected;
  for (const auto index : indices) {
    selected.push_back(all_muons.at(index));
  }
  event.set("muons", std::move(selected));
  event.set("leptonicZ", event.get<LorentzVector>("leptonicZ_" + suffix));
  apply_jme_and_select_jets(event, jme_result, variation);
  const auto &muons = event.get<std::vector<ObjectView>>("muons");
  std::vector<ObjectView> probe_jets;
  for (const auto &jet : event.get<std::vector<ObjectView>>("fatjets")) {
    if (delta_r(jet, muons[0]) > 0.8f && delta_r(jet, muons[1]) > 0.8f) {
      probe_jets.push_back(jet);
      break;  // fatjets are already sorted by corrected pT.
    }
  }
  if (probe_jets.empty()) {
    return false;
  }
  // Produce EWK weights after the selection.
  if (!event.has("genVpt")) {
    nlo_ew_weight_producer_.prepare(event);
  }

  fill_base_event_info(event, variation);
  fill_fatjet_info(event, probe_jets, 0U);
  nlo_ew_weight_producer_.fill(event, out_);
  out_.fill("passMuTrig", pass_trigger(event, config_.required_triggers));

  const auto &z = event.get<LorentzVector>("leptonicZ");
  out_.fill("leptonicZ_pt", static_cast<float>(z.Pt()));
  out_.fill("leptonicZ_mass", static_cast<float>(z.M()));
  for (std::size_t index = 0; index < muons.size(); ++index) {
    const auto prefix = "muon" + std::to_string(index) + "_";
    out_.fill(prefix + "pt", muons[index].pt());
    out_.fill(prefix + "eta", muons[index].eta());
    out_.fill(prefix + "phi", muons[index].phi());
    out_.fill(prefix + "mass", muons[index].mass());
    out_.fill(prefix + "miniIso", muons[index].get<float>("miniPFRelIso_all"));
  }

  if (event.is_mc()) {
    // Cache ID/HLT SFs and their uncertainties for each muon state.
    // JME variations use the nominal muon state and read the same cached SFs.
    const auto hlt_key = "muonHLTSF_" + suffix;
    const auto id_key = "muonIDSF_" + suffix;
    if (!event.has(hlt_key)) {
      event.set(hlt_key, muon_corrector_.high_pt_scale_factor(
          muons, "NUM_HLT_DEN_TrkHighPtLooseRelIsoProbes", 52.0f));
      event.set(id_key, muon_corrector_.scale_factor(muons, "NUM_HighPtID_DEN_TrackerMuons", 10.0f));
    }
    for (const std::string name : {"muonHLTSF", "muonIDSF"}) {
      const auto &sf = event.get<MuonSFResult>(name + "_" + suffix);
      out_.fill(name, sf.nominal);
      out_.fill(name + "_stat_up", sf.stat_up);
      out_.fill(name + "_stat_down", sf.stat_down);
      out_.fill(name + "_syst_up", sf.syst_up);
      out_.fill(name + "_syst_down", sf.syst_down);
    }
  }
  return true;
}

bool HeavyFlavZmmSampleProducer::analyze(Event &event) {
  if (!analyze_common(event)) {
    return false;
  }
  const auto jme_result = compute_jme_result(event);
  return analyze_variation(event, jme_result, JmeVariation::Nominal);
}

}  // namespace nano
