#include "nano/producers/HeavyFlavZbbSampleProducer.h"

#include "nano/core/Collection.h"
#include "nano/core/Helpers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace nano {

/*
 * Channel summary: zbb
 *
 * Purpose
 * - Select a boosted dijet phase space used for hadronic Z and heavy-flavour
 *   tagging studies.
 *
 * Event selection implemented in this producer
 * - Build loose-lepton collections for jet cleaning through shared
 *   base-producer helpers.
 * - Compute AK4/AK8/SubJet JME corrections once, then apply the requested
 *   variation before selecting the cleaned AK8 jets.
 * - Require at least two cleaned AK8 jets with pt > 200 GeV, |eta| < 2.4,
 *   and the jet-ID requirements implemented by the base producer.
 * - Rank the corrected AK8 jets by their cached `xbb_discr` values and select
 *   the two highest-score jets, requiring pt > 400/200 GeV in score order.
 * - When channels.zbb.apply_xbb_cut is enabled (disabled by default), require
 *   the score-leading jet to pass GPT > 0.7, ParticleNetLegacy > 0.9, or
 *   Run-3 ParticleNet > 0.9. Missing discriminator inputs have value -999.
 * - Require the two selected AK8 jets to satisfy |DeltaPhi| >= pi/2.
 * - Keep both selected AK8 jets for output. Store the configured HLT decisions
 *   individually without applying a trigger OR veto in this producer.
 */

HeavyFlavZbbSampleProducer::HeavyFlavZbbSampleProducer(ProducerConfig config)
    : HeavyFlavBaseProducer([&config] {
        config.channel = "zbb";
        return config;
      }()),
      nlo_ew_weight_producer_(config_) {
  const auto it = config_.channel_options.bools.find("apply_xbb_cut");
  if (it != config_.channel_options.bools.end()) {
    apply_xbb_cut_ = it->second;
  }
}

// Register common output, cached EWK weights, and the version-specific tagger masses
// for both fatjet slots. Each configured HLT decision keeps its original name.
void HeavyFlavZbbSampleProducer::begin_file() {
  HeavyFlavBaseProducer::begin_file();
  nlo_ew_weight_producer_.begin_file(out_);
  for (std::size_t index = 0; index < output_fatjet_count(); ++index) {
    const auto prefix = "fj_" + std::to_string(index + 1U) + "_";
    if (config_.nano_version == "v9" || config_.nano_version == "v15") {
      out_.branch(prefix + "pnetmass", 0.0f);
    }
    if (config_.nano_version == "v12" || config_.nano_version == "v15") {
      out_.branch(prefix + "pnetrun3mass", 0.0f);
    }
    if (config_.nano_version == "v15") {
      out_.branch(prefix + "gptmass_x2p", 0.0f);
      out_.branch(prefix + "gptmass_generic", 0.0f);
    }
  }
  for (const auto &trigger : config_.required_triggers) {
    out_.branch(trigger, false);
  }
}

// Prepare the objects and generator-level weights that are independent of JME
// variations. Cache the EWK weights and each fatjet's Xbb discriminator once
// per event so all variations reuse the same values.
bool HeavyFlavZbbSampleProducer::analyze_common(Event &event) {
  prepare_common_objects(event);
  nlo_ew_weight_producer_.prepare(event);

  // v9 uses ParticleNetMD Xbb/(Xbb+QCD), v12 uses ParticleNet XbbVsQCD,
  // and v15 uses GlobalParT3 Xbb/(Xbb+QCD). Invalid values remain negative
  // infinity so they sort last.
  for (auto &fatjet : event.collection(fatjet_name_).objects()) {
    float xbb_discr = -std::numeric_limits<float>::infinity();
    if (config_.nano_version == "v12") {
      const auto score = safe_object_float(fatjet, "particleNet_XbbVsQCD", std::numeric_limits<float>::quiet_NaN());
      if (std::isfinite(score)) {
        xbb_discr = score;
      }
    } else {
      const auto prefix = config_.nano_version == "v9" ? "particleNetMD_" : "globalParT3_";
      const auto xbb = safe_object_float(fatjet, std::string(prefix) + "Xbb", std::numeric_limits<float>::quiet_NaN());
      const auto qcd = safe_object_float(fatjet, std::string(prefix) + "QCD", std::numeric_limits<float>::quiet_NaN());
      const auto denominator = xbb + qcd;
      if (std::isfinite(xbb) && std::isfinite(qcd) && std::isfinite(denominator) && denominator > 0.0f) {
        const auto score = xbb / denominator;
        if (std::isfinite(score)) {
          xbb_discr = score;
        }
      }
    }
    fatjet.set("xbb_discr", xbb_discr);
  }
  return true;
}

// Apply one JME variation, select the two score-leading jets, and fill the
// output for accepted events. Nominal and each requested variation use the
// same cached discriminator and generator-level weights.
bool HeavyFlavZbbSampleProducer::analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) {
  apply_jme_and_select_jets(event, jme_result, variation);
  auto fatjets = event.get<std::vector<ObjectView>>("fatjets");
  constexpr float pi = 3.14159265358979323846f;
  if (fatjets.size() < 2U) {
    return false;
  }
  // Leading/subleading refer to score order. Equal scores retain the
  // corrected-pt order established by the base-producer jet selection.
  std::stable_sort(fatjets.begin(), fatjets.end(), [](const auto &a, const auto &b) {
    return a.template get<float>("xbb_discr") > b.template get<float>("xbb_discr");
  });
  if (fatjets[0].pt() <= 400.0f || fatjets[1].pt() <= 200.0f ||
      std::abs(delta_phi(fatjets[0], fatjets[1])) < 0.5f * pi) {
    return false;
  }

  if (apply_xbb_cut_) {
    // Evaluate all three taggers on the score-leading jet for this variation.
    const auto &leading_jet = fatjets[0];
    const auto read_score = [&](const std::string &attribute) {
      if (!event.has_physical_branch(fatjet_name_ + "_" + attribute)) {
        return -999.0f;
      }
      const auto score = safe_object_float(leading_jet, attribute, -999.0f);
      return std::isfinite(score) ? score : -999.0f;
    };
    const auto ratio = [&](const std::string &prefix) {
      const auto xbb = read_score(prefix + "Xbb");
      const auto qcd = read_score(prefix + "QCD");
      const auto denominator = xbb + qcd;
      if (xbb == -999.0f || qcd == -999.0f || !std::isfinite(denominator) || denominator <= 0.0f) {
        return -999.0f;
      }
      const auto score = xbb / denominator;
      return std::isfinite(score) ? score : -999.0f;
    };
    const auto pnet_legacy = ratio(config_.nano_version == "v9" ? "particleNetMD_" : "particleNetLegacy_");
    const auto pnet_run3 = read_score("particleNet_XbbVsQCD");
    const auto gpt = ratio("globalParT3_");
    event.set("xbb_discr_pnetLegacy", pnet_legacy);
    event.set("xbb_discr_pnetRun3", pnet_run3);
    event.set("xbb_discr_gpt", gpt);
    if (!(gpt > 0.7f || pnet_legacy > 0.9f || pnet_run3 > 0.9f)) {
      return false;
    }
  }
  fatjets.resize(2);

  // Base filling resets the output. Copy the cached EWK weights afterwards;
  // keep them separate from genWeight for downstream event weighting.
  fill_base_event_info(event, variation);
  nlo_ew_weight_producer_.fill(event, out_);
  for (std::size_t index = 0; index < fatjets.size(); ++index) {
    fill_fatjet_info(event, fatjets, index);
    const auto prefix = "fj_" + std::to_string(index + 1U) + "_";
    const auto &jet = fatjets[index];
    // Legacy mass is stored directly; Run-3 PNet scales the current JME mass.
    if (config_.nano_version == "v9" || config_.nano_version == "v15") {
      out_.fill(prefix + "pnetmass", jet.get<float>(config_.nano_version == "v9" ? "particleNet_mass" : "particleNetLegacy_mass"));
    }
    if (config_.nano_version == "v12" || config_.nano_version == "v15") {
      out_.fill(prefix + "pnetrun3mass", jet.mass() * jet.get<float>("particleNet_massCorr"));
    }
    // GPT regressions scale the raw mass, independent of the JME variation.
    if (config_.nano_version == "v15") {
      const auto raw_mass = jet.get<float>("rawMass");
      out_.fill(prefix + "gptmass_x2p", raw_mass * jet.get<float>("globalParT3_massCorrX2p"));
      out_.fill(prefix + "gptmass_generic", raw_mass * jet.get<float>("globalParT3_massCorrGeneric"));
    }
  }
  // Preserve each input trigger decision; missing branches are false.
  // Despite the configuration name, required_triggers is not an event veto here.
  for (const auto &trigger : config_.required_triggers) {
    out_.fill(trigger, safe_bool(event, trigger));
  }
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
