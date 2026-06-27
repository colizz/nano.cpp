#include "nano/producers/HeavyFlavMinimalProducer.h"

#include "nano/core/Collection.h"

#include <stdexcept>

namespace nano {

namespace {

float get_channel_option(const ProducerConfig &config, const std::string &key) {
  const auto it = config.channel_options.numbers.find(key);
  if (it == config.channel_options.numbers.end()) {
    throw std::runtime_error("Missing numeric channel option channels." + config.channel + "." + key);
  }
  return static_cast<float>(it->second);
}

}  // namespace

/*
 * Channel summary: minimal
 *
 * Purpose
 * - Keep a minimally selected boosted-AK8 control stream.
 * - Provide corrected leading-fatjet variables without lepton, MET, W, b-jet,
 *   trigger, or topology requirements.
 *
 * Event selection implemented in this producer
 * - Build loose-lepton collections for standard AK4/AK8 jet cleaning through
 *   `HeavyFlavBaseProducer::prepare_common_objects()`.
 * - Prepare raw AK4/AK8/SubJet kinematics and fatjet gen matching through the
 *   same shared helper.
 * - Run AK4/AK8/SubJet JME corrections and MET propagation through
 *   `HeavyFlavBaseProducer::apply_jme_and_select_jets()`.
 * - Require at least one cleaned AK8 fatjet passing the shared base fatjet
 *   quality selection.
 * - Require the leading cleaned AK8 fatjet to satisfy
 *   `pt > channels.minimal.leading_fatjet_pt_min`, default 400 GeV.
 * - Keep only that leading AK8 fatjet for output.
 */

HeavyFlavMinimalProducer::HeavyFlavMinimalProducer(ProducerConfig config)
    : HeavyFlavBaseProducer([&config] {
        config.channel = "minimal";
        return config;
      }()) {}

bool HeavyFlavMinimalProducer::analyze_common(Event &event) {
  prepare_common_objects(event);
  return true;
}

bool HeavyFlavMinimalProducer::analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) {
  apply_jme_and_select_jets(event, jme_result, variation);

  const auto fatjets = event.get<std::vector<ObjectView>>("fatjets");
  if (fatjets.empty()) {
    return false;
  }
  const auto leading_fatjet = fatjets.front();
  if (leading_fatjet.pt() <= get_channel_option(config_, "leading_fatjet_pt_min")) {
    return false;
  }

  fill_base_event_info(event, variation);
  fill_fatjet_info(event, std::vector<ObjectView>{leading_fatjet});
  return true;
}

bool HeavyFlavMinimalProducer::analyze(Event &event) {
  if (!analyze_common(event)) {
    return false;
  }
  const auto jme_result = compute_jme_result(event);
  return analyze_variation(event, jme_result, JmeVariation::Nominal);
}

}  // namespace nano
