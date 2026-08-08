#include "nano/producers/HeavyFlavZmmSampleProducer.h"

#include "nano/core/Collection.h"
#include "nano/core/Helpers.h"

#include <algorithm>
#include <cmath>

namespace nano {

namespace {

void book_sf(OutputModel &out, const std::string &name) {
  out.branch(name, 1.0f);
  out.branch(name + "_stat_up", 1.0f);
  out.branch(name + "_stat_down", 1.0f);
  out.branch(name + "_syst_up", 1.0f);
  out.branch(name + "_syst_down", 1.0f);
}

void fill_sf(OutputModel &out, const std::string &name, const MuonSFResult &sf) {
  out.fill(name, sf.nominal);
  out.fill(name + "_stat_up", sf.stat_up);
  out.fill(name + "_stat_down", sf.stat_down);
  out.fill(name + "_syst_up", sf.syst_up);
  out.fill(name + "_syst_down", sf.syst_down);
}

}  // namespace

/*
 * Channel summary: zmm
 *
 * Purpose
 * - Select boosted Z->mumu events for a clean recoil-jet control sample.
 *
 * Event selection implemented in this producer
 * - Apply the configured muon scale correction and MC resolution smearing.
 * - Require exactly two isolated opposite-sign muons with pt > 60/30 GeV.
 * - Require dimuon pt > 450 GeV and 70 < mass < 110 GeV.
 * - Require a corrected AK8 jet separated from both muons by DeltaR > 0.8.
 * - Keep only the leading separated AK8 jet for output.
 */

HeavyFlavZmmSampleProducer::HeavyFlavZmmSampleProducer(ProducerConfig config)
    : HeavyFlavBaseProducer([&config] {
        config.channel = "zmm";
        return config;
      }()),
      muon_correction_(config_) {}

void HeavyFlavZmmSampleProducer::begin_file() {
  HeavyFlavBaseProducer::begin_file();
  out_.branch("passMuTrig", false);
  out_.branch("leptonicZ_pt", 0.0f);
  out_.branch("leptonicZ_mass", 0.0f);
  out_.branch("lheVpt", -1.0f);
  out_.branch("genVpt", -1.0f);
  for (int index = 0; index < 2; ++index) {
    const auto prefix = "muon" + std::to_string(index) + "_";
    out_.branch(prefix + "pt", 0.0f);
    out_.branch(prefix + "eta", 0.0f);
    out_.branch(prefix + "phi", 0.0f);
    out_.branch(prefix + "mass", 0.0f);
    out_.branch(prefix + "miniIso", 0.0f);
  }
  for (const auto *name : {"muonHLTSF", "muonIDSF", "muonISOSF", "muonIDISOSF"}) {
    book_sf(out_, name);
  }
}

bool HeavyFlavZmmSampleProducer::analyze_common(Event &event) {
  auto muons = event.collection("Muon").objects();
  muon_correction_.correct(event, muons);

  std::vector<ObjectView> selected;
  for (auto &muon : muons) {
    const auto passes_id = (muon.pt() > 15.0f && muon.get<bool>("looseId")) ||
                           (muon.pt() > 30.0f && muon.get<std::int32_t>("highPtId") != 0);
    if (passes_id && std::abs(muon.eta()) < 2.4f && muon.get<std::int32_t>("pfIsoId") > 1) {
      selected.push_back(muon);
    }
  }
  if (selected.size() != 2U) {
    return false;
  }
  std::sort(selected.begin(), selected.end(), [](const auto &a, const auto &b) { return a.pt() > b.pt(); });
  if (selected[0].pt() < 60.0f || selected[1].pt() < 30.0f ||
      selected[0].get<std::int32_t>("charge") * selected[1].get<std::int32_t>("charge") > 0) {
    return false;
  }

  const auto z = selected[0].p4() + selected[1].p4();
  if (z.Pt() < 450.0 || z.M() < 70.0 || z.M() > 110.0) {
    return false;
  }
  event.set("muons", selected);
  event.set("leptonicZ", z);
  prepare_common_objects(event);
  return true;
}

bool HeavyFlavZmmSampleProducer::analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) {
  apply_jme_and_select_jets(event, jme_result, variation);
  const auto &muons = event.get<std::vector<ObjectView>>("muons");
  std::vector<ObjectView> probe_jets;
  for (const auto &jet : event.get<std::vector<ObjectView>>("fatjets")) {
    if (delta_r(jet, muons[0]) > 0.8f && delta_r(jet, muons[1]) > 0.8f) {
      probe_jets.push_back(jet);
    }
  }
  if (probe_jets.empty()) {
    return false;
  }
  probe_jets.resize(1);

  fill_base_event_info(event, variation);
  fill_fatjet_info(event, probe_jets, 0U);
  out_.fill("passMuTrig", pass_trigger(event, config_.required_triggers));
  const auto &z = event.get<LorentzVector>("leptonicZ");
  out_.fill("leptonicZ_pt", static_cast<float>(z.Pt()));
  out_.fill("leptonicZ_mass", static_cast<float>(z.M()));
  out_.fill("lheVpt", get_lhe_v_pt(event));
  out_.fill("genVpt", get_gen_v_pt(event));
  for (std::size_t index = 0; index < muons.size(); ++index) {
    const auto prefix = "muon" + std::to_string(index) + "_";
    out_.fill(prefix + "pt", muons[index].pt());
    out_.fill(prefix + "eta", muons[index].eta());
    out_.fill(prefix + "phi", muons[index].phi());
    out_.fill(prefix + "mass", muons[index].mass());
    out_.fill(prefix + "miniIso", muons[index].get<float>("miniPFRelIso_all"));
  }

  if (event.is_mc()) {
    const auto hlt = muon_correction_.scale_factor(
        muons, "NUM_Mu50_or_CascadeMu100_or_HighPtTkMu100_DEN_CutBasedIdTrkHighPt_and_TkIsoLoose", 52.0f);
    const auto id = muon_correction_.scale_factor(muons, "NUM_HighPtID_DEN_TrackerMuons", 10.0f);
    const auto iso = muon_correction_.scale_factor(muons, "NUM_LooseRelTkIso_DEN_HighPtID", 10.0f);
    fill_sf(out_, "muonHLTSF", hlt);
    fill_sf(out_, "muonIDSF", id);
    fill_sf(out_, "muonISOSF", iso);
    fill_sf(out_, "muonIDISOSF",
            {id.nominal * iso.nominal, id.stat_up * iso.stat_up, id.stat_down * iso.stat_down,
             id.syst_up * iso.syst_up, id.syst_down * iso.syst_down});
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
