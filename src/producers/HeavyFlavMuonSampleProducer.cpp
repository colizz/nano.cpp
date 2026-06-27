#include "nano/producers/HeavyFlavMuonSampleProducer.h"

#include "nano/core/Collection.h"
#include "nano/core/Helpers.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace nano {

/*
 * Channel summary: muon
 *
 * Purpose
 * - Select a phase space dominated by semi-leptonic ttbar events.
 * - Enrich boosted hadronic top and W jets.
 * - Produce a flat ntuple suited for deriving top/W tagging scale factors and
 *   for top/W-jet JMS and JMR studies.
 *
 * Event selection implemented in this producer
 * - Require exactly one tight muon with:
 *   pt > 55 GeV, |eta| < 2.4, |dxy| < 0.2, |dz| < 0.5,
 *   `tightId`, and `miniPFRelIso_all < 0.10`.
 * - Build loose-lepton collections for jet cleaning through
 *   `HeavyFlavBaseProducer::select_leptons()`.
 * - Compute AK4/AK8/SubJet JME corrections once, then apply the requested JME
 *   variation and MET propagation through shared base-producer helpers.
 * - Require corrected MET > 50 GeV.
 * - Reconstruct the leptonic W from the selected muon and MET and require
 *   `pT(W_lep) > 100 GeV`.
 * - Require at least one AK4 jet passing the configured medium b-tag working
 *   point and satisfying `|DeltaPhi(jet, muon)| < 2`.
 * - Require at least one probe AK8 jet satisfying `|DeltaPhi(fatjet, muon)| > 2`.
 * - Keep only the leading probe AK8 jet for output.
 */

HeavyFlavMuonSampleProducer::HeavyFlavMuonSampleProducer(ProducerConfig config)
    : HeavyFlavBaseProducer([&config] {
        config.channel = "muon";
        return config;
      }()) {}

void HeavyFlavMuonSampleProducer::begin_file() {
  HeavyFlavBaseProducer::begin_file();
  out_.branch("passMuTrig", false);
  out_.branch("muon_pt", 0.0f);
  out_.branch("muon_eta", 0.0f);
  out_.branch("muon_miniIso", 0.0f);
  out_.branch("leptonicW_pt", 0.0f);
}

// Run the part of the muon-channel selection that is independent of JME
// variations. It selects the single tight high-pt muon and prepares the loose
// lepton and fatjet gen-matching state through shared base-producer helpers.
bool HeavyFlavMuonSampleProducer::analyze_common(Event &event) {
  auto muons = event.collection("Muon").objects();
  std::vector<ObjectView> selected_muons;
  for (auto &mu : muons) {
    if (mu.pt() > 55.0f && std::abs(mu.eta()) < 2.4f && std::abs(mu.get<float>("dxy")) < 0.2f &&
        std::abs(mu.get<float>("dz")) < 0.5f && mu.get<bool>("tightId") && mu.get<float>("miniPFRelIso_all") < 0.10f) {
      selected_muons.push_back(mu);
    }
  }
  if (selected_muons.size() != 1U) {
    return false;
  }
  event.set("muons", selected_muons);

  prepare_common_objects(event);
  return true;
}

// Apply one JME variation, run the variation-dependent selections, and fill
// the output model for that accepted event. This is called once for nominal
// running and once per requested variation in multi-output mode.
bool HeavyFlavMuonSampleProducer::analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) {
  apply_jme_and_select_jets(event, jme_result, variation);

  if (event.get<float>("met_pt") < 50.0f) {
    return false;
  }

  auto mu = event.get<std::vector<ObjectView>>("muons").front();
  event.set("mu", mu);
  const auto leptonic_w = polar_p4(mu) + met_p4(event.get<float>("met_pt"), event.get<float>("met_phi"));
  event.set("leptonicW", leptonic_w);
  if (leptonic_w.Pt() < 100.0f) {
    return false;
  }

  const auto ak4jets = event.get<std::vector<ObjectView>>("ak4jets");
  std::vector<ObjectView> bjets;
  for (auto &jet : ak4jets) {
    const auto btag_value = jet.get<float>(config_.btag_config.branch);
    if (btag_value > config_.btag_config.medium && std::abs(delta_phi(jet, mu)) < 2.0f) {
      bjets.push_back(jet);
    }
  }
  if (bjets.empty()) {
    return false;
  }
  event.set("bjets", bjets);

  const auto fatjets = event.get<std::vector<ObjectView>>("fatjets");
  std::vector<ObjectView> probe_jets;
  for (auto &fj : fatjets) {
    if (std::abs(delta_phi(fj, mu)) > 2.0f) {
      probe_jets.push_back(fj);
    }
  }
  if (probe_jets.empty()) {
    return false;
  }
  // The muon channel keeps only the leading probe jet in the current workflow.
  probe_jets.erase(probe_jets.begin() + 1, probe_jets.end());

  fill_base_event_info(event, variation);
  fill_fatjet_info(event, probe_jets);

  out_.fill("passMuTrig", pass_trigger(event, config_.required_triggers));
  out_.fill("muon_pt", mu.pt());
  out_.fill("muon_eta", mu.eta());
  out_.fill("muon_miniIso", mu.get<float>("miniPFRelIso_all"));
  out_.fill("leptonicW_pt", static_cast<float>(leptonic_w.Pt()));
  return true;
}

bool HeavyFlavMuonSampleProducer::analyze(Event &event) {
  if (!analyze_common(event)) {
    return false;
  }
  const auto jme_result = compute_jme_result(event);
  return analyze_variation(event, jme_result, JmeVariation::Nominal);
}

}  // namespace nano
