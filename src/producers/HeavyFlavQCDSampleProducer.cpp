#include "nano/producers/HeavyFlavQCDSampleProducer.h"

#include "nano/core/Collection.h"
#include "nano/core/Helpers.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace nano {

namespace {

bool get_channel_bool(const ProducerConfig &config, const std::string &key) {
  // Resolve a required boolean from the active channel configuration and fail
  // early when the runtime card does not provide it.
  const auto it = config.channel_options.bools.find(key);
  if (it == config.channel_options.bools.end()) {
    throw std::runtime_error("Missing boolean channel option channels." +
                             config.channel + "." + key);
  }
  return it->second;
}

std::string fatjet_sv_branch_name(std::size_t fatjet_index,
                                  std::string_view variable) {
  // Convert the zero-based output slot and a logical variable name into the
  // flat-tree convention, for example slot 0 + "nsv" becomes "fj_1_nsv".
  return "fj_" + std::to_string(fatjet_index + 1U) + "_" +
         std::string(variable);
}

float corrected_sv_mass(const ObjectView &sv) {
  // Compute the pAngle-based corrected SV mass used by the NanoHRTTools
  // reference and store it later as an SV-level derived attribute.
  const auto projected_momentum =
      static_cast<float>(polar_p4(sv).P()) * std::sin(sv.get<float>("pAngle"));
  const auto mass = sv.mass();
  return std::sqrt(mass * mass + projected_momentum * projected_momentum) +
         projected_momentum;
}

} // namespace

/*
 * Channel summary: qcd
 *
 * Purpose
 * - Select a dijet phase space dominated by multijet production.
 * - Retain both leading boosted AK8 jets for heavy-flavour tagger studies.
 * - Require displaced-vertex content in the probe jets by default.
 *
 * Event selection implemented in this producer
 * - Build loose leptons and use the shared AK4/AK8 lepton cleaning.
 * - Apply the shared AK4/AK8/SubJet JME corrections and MET propagation.
 * - Require at least two selected AK8 fatjets and keep the leading two in
 *   corrected-pt order.
 * - Mark a fatjet qualified when 50 < msoftdrop < 200 GeV.
 * - When channels.qcd.apply_sv_criteria is enabled, reject events with fewer
 * than two secondary vertices. Fatjet qualification then additionally requires
 *   every linked subjet to have at least one matched SV; in the usual two-
 *   subjet case, both sj1 and sj2 must satisfy this matching requirement.
 * - Require at least one of the two leading fatjets to be qualified.
 * - When channels.qcd.fill_sv is enabled, store matched-SV counts, track sums,
 *   leading-SV properties, and corrected SV masses for both fatjet slots.
 */

HeavyFlavQCDSampleProducer::HeavyFlavQCDSampleProducer(ProducerConfig config)
    : HeavyFlavBaseProducer([&config] {
        config.channel = "qcd";
        return config;
      }()),
      apply_sv_criteria_(get_channel_bool(config_, "apply_sv_criteria")),
      fill_sv_(get_channel_bool(config_, "fill_sv")) {}

void HeavyFlavQCDSampleProducer::begin_file() {
  HeavyFlavBaseProducer::begin_file();
  out_.branch("passHTTrig", false);
  if (fill_sv_) {
    define_fatjet_sv_branches(0U);
    define_fatjet_sv_branches(1U);
  }
}

void HeavyFlavQCDSampleProducer::select_secondary_vertices(Event &event) const {
  // Build the pt-ordered SV collection used throughout the event and attach
  // the corrected mass to each SV when detailed SV output is enabled.
  auto secondary_vertices = event.collection("SV").objects();
  if (fill_sv_) {
    for (auto &sv : secondary_vertices) {
      sv.set("masscor", corrected_sv_mass(sv));
    }
  }
  std::sort(
      secondary_vertices.begin(), secondary_vertices.end(),
      [](const auto &lhs, const auto &rhs) { return lhs.pt() > rhs.pt(); });
  event.set("secondary_vertices", std::move(secondary_vertices));
}

void HeavyFlavQCDSampleProducer::match_secondary_vertices(
    std::vector<ObjectView> &fatjets,
    const std::vector<ObjectView> &secondary_vertices) const {
  // Match the ordered SVs to each fatjet and its linked subjets, then attach
  // the match lists and derived counts directly to the corresponding objects.
  for (auto &fatjet : fatjets) {
    std::vector<ObjectView> matched_vertices;
    std::int32_t ntracks = 0;
    std::int32_t ntracks_sv12 = 0;
    std::int32_t nsv_ptgt25 = 0;
    std::int32_t nsv_ptgt50 = 0;
    for (const auto &sv : secondary_vertices) {
      if (delta_r(sv, fatjet) < jet_cone_size_) {
        matched_vertices.push_back(sv);
        if (fill_sv_) {
          const auto sv_ntracks = sv.get<std::int32_t>("ntracks");
          ntracks += sv_ntracks;
          if (matched_vertices.size() <= 2U) {
            ntracks_sv12 += sv_ntracks;
          }
          nsv_ptgt25 += sv.pt() > 25.0f ? 1 : 0;
          nsv_ptgt50 += sv.pt() > 50.0f ? 1 : 0;
        }
      }
    }
    // Define the matched SV list (sv_list) for the fatjet.
    fatjet.set("sv_list", std::move(matched_vertices));
    if (fill_sv_) {
      const auto &fatjet_vertices =
          fatjet.extra<std::vector<ObjectView>>("sv_list");
      fatjet.set("nsv", static_cast<std::int32_t>(fatjet_vertices.size()));
      fatjet.set("nsv_ptgt25", nsv_ptgt25);
      fatjet.set("nsv_ptgt50", nsv_ptgt50);
      fatjet.set("ntracks", ntracks);
      fatjet.set("ntracks_sv12", ntracks_sv12);
    }

    auto subjets = fatjet.extra<std::vector<ObjectView>>("subjets");
    const auto match_radius =
        subjets.size() == 2U
            ? std::min(0.4f, 0.5f * delta_r(subjets[0], subjets[1]))
            : 0.4f;
    for (auto &subjet : subjets) {
      std::vector<ObjectView> subjet_vertices;
      std::int32_t subjet_ntracks = 0;
      for (const auto &sv : secondary_vertices) {
        if (delta_r(sv, subjet) < match_radius) {
          subjet_vertices.push_back(sv);
          if (fill_sv_) {
            subjet_ntracks += sv.get<std::int32_t>("ntracks");
          }
        }
      }
      // For each subjet, define the matched SV list (sv_list) as well.
      subjet.set("sv_list", std::move(subjet_vertices));
      if (fill_sv_) {
        const auto &matched_subjet_vertices =
            subjet.extra<std::vector<ObjectView>>("sv_list");
        subjet.set("nsv",
                   static_cast<std::int32_t>(matched_subjet_vertices.size()));
        subjet.set("ntracks", subjet_ntracks);
      }
    }
    fatjet.set("subjets", subjets);

    float sj12_masscor_dxysig = 0.0f;
    if (fill_sv_ && subjets.size() >= 2U) {
      const auto &sj1_vertices =
          subjets[0].extra<std::vector<ObjectView>>("sv_list");
      const auto &sj2_vertices =
          subjets[1].extra<std::vector<ObjectView>>("sv_list");
      if (!sj1_vertices.empty() && !sj2_vertices.empty()) {
        const auto &sj1_sv = sj1_vertices.front();
        const auto &sj2_sv = sj2_vertices.front();
        const auto &selected_sv =
            sj1_sv.get<float>("dxySig") > sj2_sv.get<float>("dxySig") ? sj1_sv
                                                                      : sj2_sv;
        sj12_masscor_dxysig = selected_sv.get<float>("masscor");
      }
    }
    if (fill_sv_) {
      fatjet.set("sj12_masscor_dxysig", sj12_masscor_dxysig);
    }
  }
}

void HeavyFlavQCDSampleProducer::define_fatjet_sv_branches(
    std::size_t fatjet_index) {
  // Keep logical variable names separate from the output slot so the same
  // schema can be generated consistently for fj_1 and fj_2.
  constexpr std::string_view fatjet_int_variables[]{
      "nsv", "nsv_ptgt25", "nsv_ptgt50", "ntracks", "ntracks_sv12",
  };
  constexpr std::string_view subjet_int_variables[]{
      "ntracks",
      "nsv",
      "sv1_ntracks",
  };
  constexpr std::string_view subjet_float_variables[]{
      "sv1_pt",   "sv1_mass",    "sv1_masscor",  "sv1_dxy",    "sv1_dxysig",
      "sv1_dlen", "sv1_dlensig", "sv1_chi2ndof", "sv1_pangle",
  };

  // Define fatjet-level branches such as fj_1_nsv, fj_1_ntracks_sv12, and the
  // corresponding fj_2 branches.
  for (const auto variable : fatjet_int_variables) {
    out_.branch(fatjet_sv_branch_name(fatjet_index, variable), std::int32_t{0});
  }

  // Define both subjet blocks, for example fj_1_sj1_ntracks,
  // fj_1_sj2_sv1_masscor, fj_2_sj1_nsv, and fj_2_sj2_sv1_pangle.
  for (std::size_t subjet_index = 0; subjet_index < 2U; ++subjet_index) {
    const auto subjet_prefix = "sj" + std::to_string(subjet_index + 1U) + "_";
    for (const auto variable : subjet_int_variables) {
      out_.branch(fatjet_sv_branch_name(fatjet_index,
                                        subjet_prefix + std::string(variable)),
                  std::int32_t{0});
    }
    for (const auto variable : subjet_float_variables) {
      out_.branch(fatjet_sv_branch_name(fatjet_index,
                                        subjet_prefix + std::string(variable)),
                  0.0f);
    }
  }
  // Define the combined leading-SV branch, for example
  // fj_1_sj12_masscor_dxysig or fj_2_sj12_masscor_dxysig.
  out_.branch(fatjet_sv_branch_name(fatjet_index, "sj12_masscor_dxysig"), 0.0f);
}

void HeavyFlavQCDSampleProducer::fill_fatjet_sv_info(
    const std::vector<ObjectView> &fatjets, std::size_t fatjet_index) {
  // Fill the QCD-only SV block for one qualified fatjet slot directly from
  // derived attributes attached during secondary-vertex matching.
  if (fatjet_index >= fatjets.size() ||
      !fatjets[fatjet_index].get<bool>("is_qualified")) {
    return;
  }

  const auto &fatjet = fatjets[fatjet_index];
  const auto fill = [this, fatjet_index](std::string_view variable,
                                         const auto &value) {
    out_.fill(fatjet_sv_branch_name(fatjet_index, variable), value);
  };
  fill("nsv", fatjet.get<std::int32_t>("nsv"));
  fill("nsv_ptgt25", fatjet.get<std::int32_t>("nsv_ptgt25"));
  fill("nsv_ptgt50", fatjet.get<std::int32_t>("nsv_ptgt50"));
  fill("ntracks", fatjet.get<std::int32_t>("ntracks"));
  fill("ntracks_sv12", fatjet.get<std::int32_t>("ntracks_sv12"));

  const auto &subjets = fatjet.extra<std::vector<ObjectView>>("subjets");
  const auto num_output_subjets = std::min<std::size_t>(2U, subjets.size());
  for (std::size_t subjet_index = 0; subjet_index < num_output_subjets;
       ++subjet_index) {
    const auto prefix = "sj" + std::to_string(subjet_index + 1U) + "_";
    const auto &subjet = subjets[subjet_index];
    fill(prefix + "ntracks", subjet.get<std::int32_t>("ntracks"));
    fill(prefix + "nsv", subjet.get<std::int32_t>("nsv"));
    const auto &matched_vertices =
        subjet.extra<std::vector<ObjectView>>("sv_list");
    if (matched_vertices.empty()) {
      continue;
    }

    const auto &sv = matched_vertices.front();
    fill(prefix + "sv1_pt", sv.pt());
    fill(prefix + "sv1_mass", sv.mass());
    fill(prefix + "sv1_masscor", sv.get<float>("masscor"));
    fill(prefix + "sv1_ntracks", sv.get<std::int32_t>("ntracks"));
    fill(prefix + "sv1_dxy", sv.get<float>("dxy"));
    fill(prefix + "sv1_dxysig", sv.get<float>("dxySig"));
    fill(prefix + "sv1_dlen", sv.get<float>("dlen"));
    fill(prefix + "sv1_dlensig", sv.get<float>("dlenSig"));
    fill(prefix + "sv1_chi2ndof", sv.get<float>("chi2"));
    fill(prefix + "sv1_pangle", sv.get<float>("pAngle"));
  }
  fill("sj12_masscor_dxysig", fatjet.get<float>("sj12_masscor_dxysig"));
}

bool HeavyFlavQCDSampleProducer::analyze_common(Event &event) {
  prepare_common_objects(event);
  select_secondary_vertices(event);
  return true;
}

bool HeavyFlavQCDSampleProducer::analyze_variation(
    Event &event, const JmeEventResult &jme_result, JmeVariation variation) {
  apply_jme_and_select_jets(event, jme_result, variation);

  auto probe_jets = event.get<std::vector<ObjectView>>("fatjets");
  if (probe_jets.size() < 2U) {
    return false;
  }
  probe_jets.resize(2U);

  const auto &secondary_vertices =
      event.get<std::vector<ObjectView>>("secondary_vertices");
  if (apply_sv_criteria_ && secondary_vertices.size() < 2U) {
    return false;
  }
  match_secondary_vertices(probe_jets, secondary_vertices);

  // Require at least one linked subjet and an SV match for every linked subjet.
  const auto pass_sv_requirement = [](const ObjectView &fatjet) {
    const auto &subjets = fatjet.extra<std::vector<ObjectView>>("subjets");
    return !subjets.empty() &&
           std::all_of(
               subjets.begin(), subjets.end(), [](const ObjectView &subjet) {
                 return !subjet.extra<std::vector<ObjectView>>("sv_list")
                             .empty();
               });
  };
  bool has_qualified_fatjet = false;
  for (auto &fatjet : probe_jets) {
    const auto softdrop_mass = fatjet.get<float>("msoftdrop");
    const auto pass_sv = !apply_sv_criteria_ || pass_sv_requirement(fatjet);
    const auto is_qualified =
        softdrop_mass > 50.0f && softdrop_mass < 200.0f && pass_sv;
    fatjet.set("is_qualified", is_qualified);
    has_qualified_fatjet = has_qualified_fatjet || is_qualified;
  }
  if (!has_qualified_fatjet) {
    return false;
  }

  // Reset and fill the shared event output, the two common fatjet blocks, the
  // optional QCD SV blocks, and the channel trigger decision.
  fill_base_event_info(event, variation);
  fill_fatjet_info(event, probe_jets, 0U);
  fill_fatjet_info(event, probe_jets, 1U);
  if (fill_sv_) {
    fill_fatjet_sv_info(probe_jets, 0U);
    fill_fatjet_sv_info(probe_jets, 1U);
  }
  out_.fill("passHTTrig", pass_trigger(event, config_.required_triggers));
  return true;
}

bool HeavyFlavQCDSampleProducer::analyze(Event &event) {
  if (!analyze_common(event)) {
    return false;
  }
  const auto jme_result = compute_jme_result(event);
  return analyze_variation(event, jme_result, JmeVariation::Nominal);
}

} // namespace nano
