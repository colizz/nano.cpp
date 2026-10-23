#include "nano/producers/HeavyFlavTrigSFProducer.h"

#include "nano/core/Collection.h"
#include "nano/core/Helpers.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace nano {

/*
 * Channel: trigsf
 * Measure boosted AK8 event-trigger efficiencies in a semileptonic ttbar
 * control region, following references/trigger-sf/derive/selection.hxx while
 * retaining the framework's JME corrections and common ntuple output.
 * - Require the configured reference muon trigger for both data and MC.
 * - Exactly one tight muon: pt > 55, |eta| < 2.4, pfRelIso04_all < 0.15.
 * - Veto additional loose muons (pt > 10, |eta| < 2.4, PF iso < 0.25)
 *   and cutBased >= 2 electrons (pt > 10, |eta| < 2.5).
 * - At least two tight-ID AK4 jets with pt > 30, |eta| < 2.4,
 *   DeltaR(mu, jet) > 0.4 and the configured loose b-tag working point.
 * - Keep the leading tight-ID AK8 with pt > 200, |eta| < 2.4 and
 *   DeltaR(mu, jet) > 0.8. No MET, W-pt, mass or DeltaPhi requirements.
 * - Copy studied_triggers under their original names without selecting on them.
 */

HeavyFlavTrigSFProducer::HeavyFlavTrigSFProducer(ProducerConfig config)
    : HeavyFlavBaseProducer([&config] {
        config.channel = "trigsf";
        return config;
      }()),
      top_pt_weight_producer_(config_.era) {
  if (config_.required_triggers.empty()) {
    throw std::runtime_error("trigsf requires a reference trigger in required_triggers");
  }
}

void HeavyFlavTrigSFProducer::begin_file() {
  HeavyFlavBaseProducer::begin_file();
  top_pt_weight_producer_.begin_file(out_);
  out_.branch("passMuTrig", false);
  out_.branch("muon_pt", 0.0f);
  out_.branch("muon_eta", 0.0f);
  out_.branch("muon_pfRelIso04", 0.0f);
  out_.branch("nAK8", std::int32_t{0});
  for (const auto &trigger : config_.studied_triggers) {
    out_.branch(trigger, false);
  }
}

bool HeavyFlavTrigSFProducer::analyze_common(Event &event) {
  // A missing studied path must not masquerade as a failed trigger decision.
  for (const auto &trigger : config_.studied_triggers) {
    if (!event.has_physical_branch(trigger)) {
      throw std::runtime_error("Missing studied trigger branch: " + trigger);
    }
  }
  if (!pass_trigger(event, config_.required_triggers)) {
    return false;
  }

  auto muons = event.collection("Muon").objects();
  std::vector<ObjectView> selected_muons;
  for (auto &mu : muons) {
    if (mu.pt() > 55.0f && std::abs(mu.eta()) < 2.4f && mu.get<bool>("tightId") &&
        mu.get<float>("pfRelIso04_all") < 0.15f) {
      selected_muons.push_back(mu);
    }
  }
  if (selected_muons.size() != 1U) {
    return false;
  }
  const auto mu = selected_muons.front();
  for (auto &other : muons) {
    if (other.index() != mu.index() && other.pt() > 10.0f && std::abs(other.eta()) < 2.4f &&
        other.get<bool>("looseId") && other.get<float>("pfRelIso04_all") < 0.25f) {
      return false;
    }
  }
  for (auto &el : event.collection("Electron").objects()) {
    if (el.pt() > 10.0f && std::abs(el.eta()) < 2.5f && el.get<std::int32_t>("cutBased") >= 2) {
      return false;
    }
  }
  event.set("muons", selected_muons);
  prepare_common_objects(event);
  top_pt_weight_producer_.prepare(event);
  return true;
}

void HeavyFlavTrigSFProducer::select_trigger_jets(Event &event) const {
  const auto mu = event.get<std::vector<ObjectView>>("muons").front();
  std::vector<ObjectView> ak4jets;
  std::vector<ObjectView> fatjets;
  // Rebuild from the full corrected collections: the shared preparation has
  // already attached subjets/masses, but its loose-lepton cleaning and AK4
  // lepton-veto ID are different from this control region's selection.
  for (auto &jet : event.collection("Jet").objects()) {
    if (jet.pt() > 30.0f && std::abs(jet.eta()) < 2.4f &&
        pass_jet_id(jet, config_.nano_version, false) && delta_r(jet, mu) > 0.4f) {
      ak4jets.push_back(jet);
    }
  }
  for (auto &fj : event.collection(fatjet_name_).objects()) {
    if (fj.pt() > 200.0f && std::abs(fj.eta()) < 2.4f &&
        pass_jet_id(fj, config_.nano_version, false) && delta_r(fj, mu) > 0.8f) {
      fatjets.push_back(fj);
    }
  }
  const auto higher_pt = [](const auto &a, const auto &b) { return a.pt() > b.pt(); };
  std::sort(ak4jets.begin(), ak4jets.end(), higher_pt);
  std::sort(fatjets.begin(), fatjets.end(), higher_pt);
  float ht = 0.0f;
  for (const auto &jet : ak4jets) {
    ht += jet.pt();
  }
  event.set("ak4jets", ak4jets);
  event.set("fatjets", fatjets);
  event.set("ht", ht);
}

bool HeavyFlavTrigSFProducer::analyze_variation(Event &event, const JmeEventResult &jme_result,
                                                 JmeVariation variation) {
  apply_jme_and_select_jets(event, jme_result, variation);
  select_trigger_jets(event);

  const auto ak4jets = event.get<std::vector<ObjectView>>("ak4jets");
  std::vector<ObjectView> bjets;
  for (auto &jet : ak4jets) {
    if (jet.get<float>(config_.btag_config.branch) > config_.btag_config.loose) {
      bjets.push_back(jet);
    }
  }
  if (bjets.size() < 2U) {
    return false;
  }
  event.set("bjets", bjets);

  auto probe_jets = event.get<std::vector<ObjectView>>("fatjets");
  if (probe_jets.empty()) {
    return false;
  }
  const auto n_ak8 = static_cast<std::int32_t>(probe_jets.size());
  probe_jets.erase(probe_jets.begin() + 1, probe_jets.end());

  fill_base_event_info(event, variation);
  fill_fatjet_info(event, probe_jets, 0U);
  top_pt_weight_producer_.fill(event, out_);
  const auto mu = event.get<std::vector<ObjectView>>("muons").front();
  out_.fill("passMuTrig", pass_trigger(event, config_.required_triggers));
  out_.fill("muon_pt", mu.pt());
  out_.fill("muon_eta", mu.eta());
  out_.fill("muon_pfRelIso04", mu.get<float>("pfRelIso04_all"));
  out_.fill("nAK8", n_ak8);
  for (const auto &trigger : config_.studied_triggers) {
    out_.fill(trigger, event.scalar<bool>(trigger));
  }
  return true;
}

bool HeavyFlavTrigSFProducer::analyze(Event &event) {
  if (!analyze_common(event)) {
    return false;
  }
  const auto jme_result = compute_jme_result(event);
  return analyze_variation(event, jme_result, JmeVariation::Nominal);
}

}  // namespace nano
