#include "nano/producers/HeavyFlavZbbSampleProducer.h"

#include "nano/core/Collection.h"
#include "nano/core/Helpers.h"

#include <algorithm>
#include <cmath>

namespace nano {

namespace {

bool bool_option(const ProducerConfig &config, const std::string &key, bool fallback) {
  const auto it = config.channel_options.bools.find(key);
  return it == config.channel_options.bools.end() ? fallback : it->second;
}

}  // namespace

/*
 * Channel summary: zbb
 *
 * Purpose
 * - Select a boosted dijet phase space used for hadronic Z and heavy-flavour
 *   tagging studies.
 *
 * Event selection implemented in this producer
 * - Require two corrected AK8 jets with pt > 450/200 GeV.
 * - Require the two leading AK8 jets to satisfy |DeltaPhi| > pi/2.
 * - Require at least two secondary vertices when require_sv_cut is enabled.
 * - Qualify a probe jet only when both linked subjets are present.
 * - Require at least one of the two leading probe jets to be qualified.
 */

HeavyFlavZbbSampleProducer::HeavyFlavZbbSampleProducer(ProducerConfig config)
    : HeavyFlavBaseProducer([&config] {
        config.channel = "zbb";
        return config;
      }()),
      require_sv_cut_(bool_option(config_, "require_sv_cut", true)) {}

void HeavyFlavZbbSampleProducer::begin_file() {
  HeavyFlavBaseProducer::begin_file();
  out_.branch("passHTTrig", false);
  out_.branch("genVpt", -1.0f);
}

bool HeavyFlavZbbSampleProducer::analyze_common(Event &event) {
  prepare_common_objects(event);
  return true;
}

bool HeavyFlavZbbSampleProducer::analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) {
  apply_jme_and_select_jets(event, jme_result, variation);
  auto jets = event.get<std::vector<ObjectView>>("fatjets");
  constexpr float pi = 3.14159265358979323846f;
  if (jets.size() < 2U || jets[0].pt() < 450.0f || jets[1].pt() < 200.0f ||
      std::abs(delta_phi(jets[0], jets[1])) < 0.5f * pi) {
    return false;
  }
  jets.resize(2);

  if (require_sv_cut_ && event.collection("SV").size() < 2U) {
    return false;
  }

  bool has_qualified_jet = false;
  for (auto &jet : jets) {
    const auto qualified = jet.extra<std::vector<ObjectView>>("subjets").size() == 2U;
    jet.set("is_qualified", qualified);
    has_qualified_jet = has_qualified_jet || qualified;
  }
  if (!has_qualified_jet) {
    return false;
  }

  fill_base_event_info(event, variation);
  fill_fatjet_info(event, jets, 0U);
  fill_fatjet_info(event, jets, 1U);
  out_.fill("passHTTrig", safe_bool(event, "HLT_AK8PFJet380_SoftDropMass30"));
  out_.fill("genVpt", get_gen_v_pt(event));
  return true;
}

bool HeavyFlavZbbSampleProducer::analyze(Event &event) {
  if (!analyze_common(event)) {
    return false;
  }
  const auto jme_result = compute_jme_result(event);
  return analyze_variation(event, jme_result, JmeVariation::Nominal);
}

}  // namespace nano
