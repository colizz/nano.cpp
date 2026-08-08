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
  out_.branch("lheVpt", -1.0f);
  out_.branch("genVpt", -1.0f);
}

bool HeavyFlavZbbSampleProducer::analyze_common(Event &event) {
  prepare_common_objects(event);
  return true;
}

bool HeavyFlavZbbSampleProducer::analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) {
  apply_jme_and_select_jets(event, jme_result, variation);
  auto fatjets = event.get<std::vector<ObjectView>>("fatjets");
  constexpr float pi = 3.14159265358979323846f;
  if (fatjets.size() < 2U || fatjets[0].pt() < 450.0f || fatjets[1].pt() < 200.0f ||
      std::abs(delta_phi(fatjets[0], fatjets[1])) < 0.5f * pi) {
    return false;
  }
  fatjets.resize(2);

  if (require_sv_cut_ && event.collection("SV").size() < 2U) {
    return false;
  }

  fill_base_event_info(event, variation);
  fill_fatjet_info(event, fatjets, 0U);
  fill_fatjet_info(event, fatjets, 1U);
  out_.fill("passHTTrig", pass_trigger(event, config_.required_triggers));
  out_.fill("lheVpt", get_lhe_v_pt(event));
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
