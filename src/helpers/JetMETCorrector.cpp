#include "nano/helpers/JetMETCorrector.h"

#include "nano/core/Collection.h"
#include "nano/core/Helpers.h"

#include "FatJetVariationsCalculator.h"
#include "JetVariationsCalculator.h"
#include "Type1METVariationsCalculator.h"

#include <ROOT/RVec.hxx>
#include <correction.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace nano {

namespace {

using RVecF = ROOT::VecOps::RVec<float>;
using RVecI = ROOT::VecOps::RVec<int>;
using correction::CorrectionSet;

bool debug_bundle_io_enabled() {
  const auto *value = std::getenv("NANO_JME_DEBUG_BUNDLE_IO");
  if (!value) {
    return false;
  }
  std::string normalized;
  for (const auto ch : std::string_view(value)) {
    normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return !(normalized.empty() || normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off");
}

template <typename T>
void debug_print_scalar(std::ostream &os, std::string_view name, const T &value) {
  os << "    " << name << " = " << value << '\n';
}

template <typename T>
void debug_print_vector(std::ostream &os, std::string_view name, const ROOT::VecOps::RVec<T> &values) {
  os << "    " << name << " [size=" << values.size() << "] = [";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      os << ", ";
    }
    os << values[i];
  }
  os << "]\n";
}

void debug_print_labels(std::ostream &os, const std::vector<std::string> &labels) {
  os << "    variations [size=" << labels.size() << "] = [";
  for (std::size_t i = 0; i < labels.size(); ++i) {
    if (i > 0) {
      os << ", ";
    }
    os << labels[i];
  }
  os << "]\n";
}

void debug_print_jets_output(std::ostream &os, std::string_view name, const std::vector<std::string> &labels,
                             const JetVariationsCalculator::result_t &result) {
  os << "[NANO_JME_DEBUG_BUNDLE_IO] " << name << " output\n";
  debug_print_labels(os, labels);
  for (std::size_t i = 0; i < result.size(); ++i) {
    const auto label = i < labels.size() ? labels[i] : std::string("variation_") + std::to_string(i);
    debug_print_vector(os, std::string("output.") + label + ".pt", result.pt(i));
    debug_print_vector(os, std::string("output.") + label + ".mass", result.mass(i));
  }
}

void debug_print_fatjets_output(std::ostream &os, std::string_view name, const std::vector<std::string> &labels,
                                const FatJetVariationsCalculator::result_t &result) {
  os << "[NANO_JME_DEBUG_BUNDLE_IO] " << name << " output\n";
  debug_print_labels(os, labels);
  for (std::size_t i = 0; i < result.size(); ++i) {
    const auto label = i < labels.size() ? labels[i] : std::string("variation_") + std::to_string(i);
    debug_print_vector(os, std::string("output.") + label + ".pt", result.pt(i));
    debug_print_vector(os, std::string("output.") + label + ".mass", result.mass(i));
    debug_print_vector(os, std::string("output.") + label + ".msoftdrop", result.msoftdrop(i));
  }
}

void debug_print_met_output(std::ostream &os, std::string_view name, const std::vector<std::string> &labels,
                            const Type1METVariationsCalculator::result_t &result) {
  os << "[NANO_JME_DEBUG_BUNDLE_IO] " << name << " output\n";
  debug_print_labels(os, labels);
  for (std::size_t i = 0; i < result.size(); ++i) {
    const auto label = i < labels.size() ? labels[i] : std::string("variation_") + std::to_string(i);
    debug_print_scalar(os, std::string("output.") + label + ".px", result.px(i));
    debug_print_scalar(os, std::string("output.") + label + ".py", result.py(i));
    debug_print_scalar(os, std::string("output.") + label + ".pt", result.pt(i));
    debug_print_scalar(os, std::string("output.") + label + ".phi", result.phi(i));
  }
}

template <typename T>
ROOT::VecOps::RVec<T> to_rvec(const std::vector<T> &values) {
  return ROOT::VecOps::RVec<T>(values.begin(), values.end());
}

RVecF zeros_f(std::size_t n) {
  return RVecF(n, 0.f);
}

RVecF filled_f(std::size_t n, float value) {
  return RVecF(n, value);
}

RVecI zeros_i(std::size_t n, int value = 0) {
  return RVecI(n, value);
}

RVecI to_rvec_int16(const std::vector<std::int16_t> &values) {
  RVecI out(values.size(), 0);
  for (std::size_t i = 0; i < values.size(); ++i) {
    out[i] = static_cast<int>(values[i]);
  }
  return out;
}

RVecI to_rvec_uint8(const std::vector<std::uint8_t> &values) {
  RVecI out(values.size(), 0);
  for (std::size_t i = 0; i < values.size(); ++i) {
    out[i] = static_cast<int>(values[i]);
  }
  return out;
}

RVecI to_rvec_int32(const std::vector<std::int32_t> &values) {
  return RVecI(values.begin(), values.end());
}

RVecI jet_id_rvec(const std::vector<ObjectView> &jets, std::string_view nano_version) {
  RVecI out;
  out.reserve(jets.size());
  for (const auto &jet : jets) {
    out.push_back(pass_jet_id(jet, nano_version, false) ? 0x2 : 0);
  }
  return out;
}

RVecI int_rvec(Event &event, std::string_view branch_name) {
  const auto *info = event.schema().find(branch_name);
  if (!info) {
    return RVecI{};
  }
  if (info->type == BranchType::kVecInt32) {
    return to_rvec_int32(event.vector<std::int32_t>(branch_name));
  }
  if (info->type == BranchType::kVecInt16) {
    return to_rvec_int16(event.vector<std::int16_t>(branch_name));
  }
  if (info->type == BranchType::kVecUInt8) {
    return to_rvec_uint8(event.vector<std::uint8_t>(branch_name));
  }
  if (info->type == BranchType::kVecUInt16) {
    RVecI out;
    const auto &values = event.vector<std::uint16_t>(branch_name);
    out.reserve(values.size());
    for (const auto value : values) {
      out.push_back(static_cast<int>(value));
    }
    return out;
  }
  throw std::runtime_error("Branch is not an integer vector: " + std::string(branch_name));
}

float get_scalar_or(Event &event, std::string_view name, float fallback) {
  return event.has_physical_branch(name) ? event.scalar<float>(name) : fallback;
}

std::pair<float, float> met_unclustered_up_delta(Event &event, std::string_view nano_version) {
  if (nano_version == "v9") {
    return {get_scalar_or(event, "MET_MetUnclustEnUpDeltaX", 0.f), get_scalar_or(event, "MET_MetUnclustEnUpDeltaY", 0.f)};
  }

  const auto pt = event.scalar<float>("PuppiMET_pt");
  const auto phi = event.scalar<float>("PuppiMET_phi");
  const auto pt_up = event.scalar<float>("PuppiMET_ptUnclusteredUp");
  const auto phi_up = event.scalar<float>("PuppiMET_phiUnclusteredUp");
  return {pt_up * std::cos(phi_up) - pt * std::cos(phi), pt_up * std::sin(phi_up) - pt * std::sin(phi)};
}

std::uint32_t get_run_number(const Event &event) {
  return event.scalar<std::uint32_t>("run");
}

int rnd_seed(const Event &event, const std::vector<ObjectView> &jets, int extra = 0) {
  const auto run = static_cast<int>(event.scalar<std::uint32_t>("run"));
  const auto lumi = static_cast<int>(event.scalar<std::uint32_t>("luminosityBlock"));
  const auto event_number = static_cast<int>(event.scalar<std::uint64_t>("event") & 0x7fffffffULL);
  int seed = (run << 20) + (lumi << 10) + event_number + extra;
  if (!jets.empty()) {
    seed += static_cast<int>(jets.front().eta() / 0.01f);
  }
  return seed;
}

std::string join_path(std::string base, std::string_view tail) {
  if (!base.empty() && base.back() == '/') {
    base.pop_back();
  }
  return base + "/" + std::string(tail);
}

bool is_local_payload_path(std::string_view path) {
  return path.rfind("/", 0) == 0 || path.rfind("./", 0) == 0 || path.rfind("data/", 0) == 0;
}

bool is_disabled_tag(std::string_view tag) {
  return tag == "none" || tag == "disabled" || tag == "off";
}

bool is_run3_or_later_era(std::string_view era) {
  return era == "2022" || era == "2022EE" || era == "2023" || era == "2023BPix" || era == "2024" || era == "2025" ||
         era == "2026";
}

std::string resolve_local_payload_path(std::string_view path) {
  namespace fs = std::filesystem;
  const fs::path payload_path{std::string(path)};
  if (payload_path.is_absolute()) {
    return payload_path.string();
  }

  auto dir = fs::current_path();
  while (true) {
    const auto candidate = dir / payload_path;
    if (fs::exists(candidate)) {
      return fs::absolute(candidate).string();
    }
    if (dir == dir.root_path()) {
      break;
    }
    dir = dir.parent_path();
  }
  return payload_path.string();
}

std::string campaign_key(const ProducerConfig &config) {
  return config.era + "_NanoAOD" + config.nano_version;
}

struct CachedCorrectionSet {
  std::unique_ptr<CorrectionSet> correction_set;
};

CachedCorrectionSet &correction_cache_for(const std::string &json_file) {
  static std::unordered_map<std::string, CachedCorrectionSet> cache;
  auto &entry = cache[json_file];
  if (!entry.correction_set) {
    entry.correction_set = CorrectionSet::from_file(json_file);
  }
  return entry;
}

std::string resolve_object_jer_tag(std::string_view object_tag, std::string_view inherited_tag) {
  if (object_tag.empty() || object_tag == "inherit") {
    return std::string(inherited_tag);
  }
  if (is_disabled_tag(object_tag)) {
    return {};
  }
  return std::string(object_tag);
}

std::vector<ObjectView> sort_by_pt(std::vector<ObjectView> objects) {
  std::sort(objects.begin(), objects.end(), [](const auto &a, const auto &b) { return a.pt() > b.pt(); });
  return objects;
}

void apply_nominal_jets(Event &event, const std::string &object_name, const ROOT::VecOps::RVec<float> &pt,
                        const ROOT::VecOps::RVec<float> &mass) {
  auto objects = event.collection(object_name).objects();
  for (std::size_t i = 0; i < objects.size() && i < pt.size(); ++i) {
    auto &obj = objects[i];
    const auto corrected_mass = std::max(0.f, mass[i]);

    obj.set("pt", pt[i]);
    obj.set("mass", corrected_mass);
    obj.set("p4", ObjectView::LorentzVector(pt[i], obj.eta(), obj.phi(), corrected_mass));
  }
}

}  // namespace

JetMETCorrector::JetMETCorrector(const ProducerConfig &config)
    : config_(config), era_setup_(build_era_setup(config)), payload_paths_(resolve_payload_paths(config, era_setup_)) {
  mc_bundle_ = std::make_unique<CalculatorBundle>(make_bundle(true));
  data_bundle_ = std::make_unique<CalculatorBundle>(make_bundle(false));
  if (should_compute_jet_veto()) {
    const auto key = campaign_key(config_);
    const auto it = config_.jet_veto_map_eras.find(key);
    if (it == config_.jet_veto_map_eras.end()) {
      throw std::runtime_error("Missing jet veto map config for " + key);
    }
    const auto json_file = is_local_payload_path(it->second.payload_subdir)
                               ? resolve_local_payload_path(it->second.payload_subdir)
                               : join_path(config_.jet_veto_map_payload_dir, it->second.payload_subdir);
    auto &cache = correction_cache_for(json_file);
    jet_veto_correction_ = cache.correction_set->at(it->second.correction_key);
  }
}

JetMETCorrector::~JetMETCorrector() = default;

JetMETCorrector::EraSetup JetMETCorrector::build_era_setup(const ProducerConfig &config) {
  const auto key = campaign_key(config);
  const auto it = config.jme_eras.find(key);
  if (it == config.jme_eras.end()) {
    throw std::runtime_error("Unsupported JME campaign: " + key);
  }
  const auto &era_cfg = it->second;
  EraSetup setup;
  setup.payload_subdir = era_cfg.payload_subdir;
  setup.met_xy_corr_era = era_cfg.met_xy_corr_era;
  setup.jes_uncertainties = era_cfg.jes_uncertainties;
  setup.jet = {era_cfg.jet.payload_subdir, era_cfg.jet.jerc_file, era_cfg.jet.algo, era_cfg.jet.jec_tag_mc, era_cfg.jet.jec_tag_data,
               era_cfg.jet.jer_tag_mc};
  setup.fatjet = {era_cfg.fatjet.payload_subdir, era_cfg.fatjet.jerc_file, era_cfg.fatjet.algo, era_cfg.fatjet.jec_tag_mc,
                  era_cfg.fatjet.jec_tag_data, era_cfg.fatjet.jer_tag_mc};
  setup.subjet = {era_cfg.subjet.payload_subdir, era_cfg.subjet.jerc_file, era_cfg.subjet.algo, era_cfg.subjet.jec_tag_mc,
                  era_cfg.subjet.jec_tag_data, era_cfg.subjet.jer_tag_mc};
  return setup;
}

JetMETCorrector::PayloadPaths JetMETCorrector::resolve_payload_paths(const ProducerConfig &config, const EraSetup &setup) {
  const auto resolve_json = [&](const ObjectSetup &object, const std::string &default_file) {
    const auto payload_subdir = object.payload_subdir.empty() ? setup.payload_subdir : object.payload_subdir;
    const auto payload_dir = is_local_payload_path(payload_subdir)
                                 ? join_path(resolve_local_payload_path(payload_subdir), "latest")
                                 : join_path(join_path(config.jme_payload_dir, payload_subdir), "latest");
    const auto jerc_file = object.jerc_file.empty() ? default_file : object.jerc_file;
    return join_path(payload_dir, jerc_file);
  };

  PayloadPaths out;
  out.payload_dir = join_path(join_path(config.jme_payload_dir, setup.payload_subdir), "latest");
  out.jet_jerc_json = resolve_json(setup.jet, "jet_jerc.json.gz");
  out.fatjet_jerc_json = resolve_json(setup.fatjet, "fatJet_jerc.json.gz");
  out.subjet_jerc_json = resolve_json(setup.subjet, "jet_jerc.json.gz");
  out.jer_smear_json = config.jme_jer_smear_json;
  return out;
}

JetMETCorrector::CalculatorBundle JetMETCorrector::make_bundle(bool is_mc) const {
  CalculatorBundle bundle;
  const auto jet_jec_tag = is_mc ? era_setup_.jet.jec_tag_mc : era_setup_.jet.jec_tag_data;
  const auto fatjet_jec_tag = is_mc ? era_setup_.fatjet.jec_tag_mc : era_setup_.fatjet.jec_tag_data;
  const auto subjet_jec_tag = is_mc ? era_setup_.subjet.jec_tag_mc : era_setup_.subjet.jec_tag_data;
  const auto jet_jer_tag = is_mc ? resolve_object_jer_tag(era_setup_.jet.jer_tag_mc, "") : std::string{};
  const auto fatjet_jer_tag = is_mc ? resolve_object_jer_tag(era_setup_.fatjet.jer_tag_mc, jet_jer_tag) : std::string{};
  const auto subjet_jer_tag = is_mc ? resolve_object_jer_tag(era_setup_.subjet.jer_tag_mc, jet_jer_tag) : std::string{};
  const auto jes_uncertainties = is_mc ? era_setup_.jes_uncertainties : std::vector<std::string>{};

  bundle.ak4_jets = std::make_unique<JetVariationsCalculator>(JetVariationsCalculator::create(
      payload_paths_.jet_jerc_json,  // jsonFile
      era_setup_.jet.algo,           // jetAlgo
      jet_jec_tag,                   // jecTag
      "L1L2L3Res",                   // jecLevel
      jes_uncertainties,             // jesUncertainties
      false,                         // addHEM2018Issue
      jet_jer_tag,                   // jerTag
      payload_paths_.jer_smear_json, // jsonFileSmearingTool
      "JERSmear",                    // smearingToolName
      false,                         // splitJER
      true,                          // doGenMatch
      0.2f,                          // genMatch_maxDR
      3.f));                         // genMatch_maxDPT
  bundle.fatjet_jets = std::make_unique<FatJetVariationsCalculator>(FatJetVariationsCalculator::create(
      payload_paths_.fatjet_jerc_json, // jsonFile
      era_setup_.fatjet.algo,          // jetAlgo
      fatjet_jec_tag,                  // jecTag
      "L1L2L3Res",                     // jecLevel
      jes_uncertainties,               // jesUncertainties
      false,                           // addHEM2018Issue
      fatjet_jer_tag,                  // jerTag
      payload_paths_.jer_smear_json,   // jsonFileSmearingTool
      "JERSmear",                      // smearingToolName
      false,                           // splitJER
      true,                            // doGenMatch
      0.4f,                            // genMatch_maxDR
      3.f,                             // genMatch_maxDPT
      payload_paths_.subjet_jerc_json, // jsonFileSubjet
      era_setup_.subjet.algo,          // jetAlgoSubjet
      subjet_jec_tag,                  // jecTagSubjet
      "L1L2L3Res"));                   // jecLevelSubjet
  bundle.subjets = std::make_unique<JetVariationsCalculator>(JetVariationsCalculator::create(
      payload_paths_.subjet_jerc_json, // jsonFile
      era_setup_.subjet.algo,        // jetAlgo
      subjet_jec_tag,                // jecTag
      "L1L2L3Res",                   // jecLevel
      jes_uncertainties,             // jesUncertainties
      false,                         // addHEM2018Issue
      subjet_jer_tag,                // jerTag
      payload_paths_.jer_smear_json, // jsonFileSmearingTool
      "JERSmear",                    // smearingToolName
      false,                         // splitJER
      true,                          // doGenMatch
      0.2f,                          // genMatch_maxDR
      3.f));                         // genMatch_maxDPT
  const auto make_met_calculator = [&](bool smear_met) {
    return std::make_unique<Type1METVariationsCalculator>(Type1METVariationsCalculator::create(
        payload_paths_.jet_jerc_json,  // jsonFile
        era_setup_.jet.algo,           // jetAlgo
        jet_jec_tag,                   // jecTag
        "L1L2L3Res",                   // jecLevel
        "L1FastJet",                   // l1JecTag
        15.f,                          // unclEnThreshold
        0.9f,                          // emEnFracThreshold
        jes_uncertainties,             // jesUncertainties
        false,                         // addHEM2018Issue
        smear_met,                     // isT1SmearedMET
        false,                         // isXYCorrMET
        "",                            // jsonXYCorrMET
        era_setup_.met_xy_corr_era,    // eraForXYCorrMET
        is_mc,                         // isMC
        jet_jer_tag,                   // jerTag
        payload_paths_.jer_smear_json, // jsonFileSmearingTool
        "JERSmear",                    // smearingToolName
        false,                         // splitJER
        true,                          // doGenMatch
        0.2f,                          // genMatch_maxDR
        3.f));                         // genMatch_maxDPT
  };
  bundle.met = make_met_calculator(config_.jme_smear_met);
  if (is_mc && !config_.jme_smear_met) {
    bundle.met_smeared = make_met_calculator(true);
  }
  return bundle;
}


std::size_t JetMETCorrector::jet_variation_index(JmeVariation variation, bool is_mc) const {
  if (!is_mc) {
    return 0U;
  }
  const auto jet_jer_tag = resolve_object_jer_tag(era_setup_.jet.jer_tag_mc, "");
  const auto jes_offset = 1U + (jet_jer_tag.empty() ? 0U : 2U);
  switch (variation) {
    case JmeVariation::JerUp:
      return jet_jer_tag.empty() ? 0U : 1U;
    case JmeVariation::JerDown:
      return jet_jer_tag.empty() ? 0U : 2U;
    case JmeVariation::JesUp:
      return jes_offset;
    case JmeVariation::JesDown:
      return jes_offset + 1U;
    case JmeVariation::Nominal:
    case JmeVariation::MetUp:
    case JmeVariation::MetDown:
      return 0U;
  }
  return 0U;
}

std::size_t JetMETCorrector::met_variation_index(const CalculatorBundle &bundle, JmeVariation variation, bool is_mc) const {
  if (!is_mc) {
    return 0U;
  }
  const auto jet_jer_tag = resolve_object_jer_tag(era_setup_.jet.jer_tag_mc, "");
  const auto jes_offset = 1U + (jet_jer_tag.empty() ? 0U : 2U);
  switch (variation) {
    case JmeVariation::JerUp:
      return jet_jer_tag.empty() ? 0U : 1U;
    case JmeVariation::JerDown:
      return jet_jer_tag.empty() ? 0U : 2U;
    case JmeVariation::JesUp:
      return jes_offset;
    case JmeVariation::JesDown:
      return jes_offset + 1U;
    case JmeVariation::MetUp:
      return bundle.met->available().size() - 2U;
    case JmeVariation::MetDown:
      return bundle.met->available().size() - 1U;
    case JmeVariation::Nominal:
      return 0U;
  }
  return 0U;
}

const JetMETCorrector::CalculatorBundle &JetMETCorrector::bundle_for_event(const Event &event) const {
  if (event.is_mc()) {
    return *mc_bundle_;
  }
  return *data_bundle_;
}

bool JetMETCorrector::should_compute_jet_veto() const {
  return config_.jet_veto_map_enabled && is_run3_or_later_era(config_.era);
}

std::int32_t JetMETCorrector::compute_jet_veto_flag(const std::vector<ObjectView> &jets) const {
  if (!should_compute_jet_veto()) {
    return -99;
  }
  if (!jet_veto_correction_) {
    throw std::runtime_error("Jet veto map correction was not initialized");
  }

  for (const auto &jet : jets) {
    if (jet.pt() <= 15.0f || !pass_jet_id(jet, config_.nano_version, true)) {
      continue;
    }
    if (safe_object_float(jet, "neEmEF", 0.0f) + safe_object_float(jet, "chEmEF", 0.0f) >= 0.9f) {
      continue;
    }
    const auto value = jet_veto_correction_->evaluate({config_.jet_veto_map_type, static_cast<double>(jet.eta()),
                                                       static_cast<double>(jet.phi())});
    if (value > 0.0) {
      return 1;
    }
  }
  return 0;
}

JmeEventResult JetMETCorrector::compute_event(Event &event) const {
  const auto is_mc = event.is_mc();
  const auto debug_bundle_io = debug_bundle_io_enabled();
  JmeEventResult result;
  result.is_mc = is_mc;

  const auto &bundle = bundle_for_event(event);
  const auto run = static_cast<int>(get_run_number(event));
  const auto rho = get_scalar_or(event, "Rho_fixedGridRhoFastjetAll", get_scalar_or(event, "fixedGridRhoFastjetAll", 0.f));
  if (debug_bundle_io) {
    std::cerr << "\n[NANO_JME_DEBUG_BUNDLE_IO] event"
              << " run=" << run
              << " luminosityBlock=" << event.scalar<std::uint32_t>("luminosityBlock")
              << " event=" << event.scalar<std::uint64_t>("event")
              << " is_mc=" << is_mc
              << " rho=" << rho << '\n';
  }

  auto ak4_jets = event.collection("Jet").objects();
  auto fatjet_jets = event.collection("FatJet").objects();
  auto subjets = event.collection("SubJet").objects();

  const auto jet_pt = to_rvec(event.vector<float>("Jet_pt"));
  const auto jet_eta = to_rvec(event.vector<float>("Jet_eta"));
  const auto jet_phi = to_rvec(event.vector<float>("Jet_phi"));
  const auto jet_mass = to_rvec(event.vector<float>("Jet_mass"));
  const auto jet_raw = to_rvec(event.vector<float>("Jet_rawFactor"));
  const auto jet_area = to_rvec(event.vector<float>("Jet_area"));
  const auto jet_jetid = jet_id_rvec(ak4_jets, config_.nano_version);
  const auto jet_genidx = is_mc ? int_rvec(event, "Jet_genJetIdx") : zeros_i(jet_pt.size(), -1);
  const auto jet_parton = is_mc ? int_rvec(event, "Jet_partonFlavour") : zeros_i(jet_pt.size(), 0);

  const auto gen_pt = is_mc ? to_rvec(event.vector<float>("GenJet_pt")) : RVecF{};
  const auto gen_eta = is_mc ? to_rvec(event.vector<float>("GenJet_eta")) : RVecF{};
  const auto gen_phi = is_mc ? to_rvec(event.vector<float>("GenJet_phi")) : RVecF{};
  const auto gen_mass = is_mc ? to_rvec(event.vector<float>("GenJet_mass")) : RVecF{};

  const auto jet_seed = rnd_seed(event, ak4_jets);
  if (debug_bundle_io) {
    std::cerr << "[NANO_JME_DEBUG_BUNDLE_IO] bundle.ak4_jets input\n";
    debug_print_vector(std::cerr, "jet_pt", jet_pt);
    debug_print_vector(std::cerr, "jet_eta", jet_eta);
    debug_print_vector(std::cerr, "jet_phi", jet_phi);
    debug_print_vector(std::cerr, "jet_mass", jet_mass);
    debug_print_vector(std::cerr, "jet_raw", jet_raw);
    debug_print_vector(std::cerr, "jet_area", jet_area);
    debug_print_vector(std::cerr, "jet_jetid", jet_jetid);
    debug_print_scalar(std::cerr, "rho", rho);
    debug_print_vector(std::cerr, "jet_genidx", jet_genidx);
    debug_print_vector(std::cerr, "jet_parton", jet_parton);
    debug_print_scalar(std::cerr, "jet_seed", jet_seed);
    debug_print_scalar(std::cerr, "run", run);
    debug_print_vector(std::cerr, "gen_pt", gen_pt);
    debug_print_vector(std::cerr, "gen_eta", gen_eta);
    debug_print_vector(std::cerr, "gen_phi", gen_phi);
    debug_print_vector(std::cerr, "gen_mass", gen_mass);
  }
  result.ak4_jets =
      bundle.ak4_jets->produce(jet_pt, jet_eta, jet_phi, jet_mass, jet_raw, jet_area, jet_jetid, rho, jet_genidx, jet_parton,
                               jet_seed, run, gen_pt, gen_eta, gen_phi, gen_mass);
  if (debug_bundle_io) {
    debug_print_jets_output(std::cerr, "bundle.ak4_jets", bundle.ak4_jets->available(), result.ak4_jets);
  }

  const auto lowpt_rawpt = to_rvec(event.vector<float>("CorrT1METJet_rawPt"));
  const auto lowpt_eta = to_rvec(event.vector<float>("CorrT1METJet_eta"));
  const auto lowpt_phi = to_rvec(event.vector<float>("CorrT1METJet_phi"));
  const auto lowpt_area = to_rvec(event.vector<float>("CorrT1METJet_area"));
  const auto lowpt_muon = to_rvec(event.vector<float>("CorrT1METJet_muonSubtrFactor"));
  const auto lowpt_zero = zeros_f(lowpt_rawpt.size());
  const auto [met_dx, met_dy] = met_unclustered_up_delta(event, config_.nano_version);

  const auto raw_met_prefix = config_.nano_version == "v9" ? "RawMET" : "RawPuppiMET";
  const auto jet_muon = to_rvec(event.vector<float>("Jet_muonSubtrFactor"));
  const auto jet_neemef = to_rvec(event.vector<float>("Jet_neEmEF"));
  const auto jet_chemef = to_rvec(event.vector<float>("Jet_chEmEF"));
  const auto raw_met_phi = event.scalar<float>(std::string(raw_met_prefix) + "_phi");
  const auto raw_met_pt = event.scalar<float>(std::string(raw_met_prefix) + "_pt");
  if (debug_bundle_io) {
    std::cerr << "[NANO_JME_DEBUG_BUNDLE_IO] bundle.met input\n";
    debug_print_vector(std::cerr, "jet_pt", jet_pt);
    debug_print_vector(std::cerr, "jet_eta", jet_eta);
    debug_print_vector(std::cerr, "jet_phi", jet_phi);
    debug_print_vector(std::cerr, "jet_mass", jet_mass);
    debug_print_vector(std::cerr, "jet_raw", jet_raw);
    debug_print_vector(std::cerr, "jet_area", jet_area);
    debug_print_vector(std::cerr, "jet_muon", jet_muon);
    debug_print_vector(std::cerr, "jet_neEmEF", jet_neemef);
    debug_print_vector(std::cerr, "jet_chEmEF", jet_chemef);
    debug_print_vector(std::cerr, "jet_jetid", jet_jetid);
    debug_print_scalar(std::cerr, "rho", rho);
    debug_print_vector(std::cerr, "jet_genidx", jet_genidx);
    debug_print_vector(std::cerr, "jet_parton", jet_parton);
    debug_print_scalar(std::cerr, "jet_seed", jet_seed);
    debug_print_scalar(std::cerr, "run", run);
    debug_print_vector(std::cerr, "gen_pt", gen_pt);
    debug_print_vector(std::cerr, "gen_eta", gen_eta);
    debug_print_vector(std::cerr, "gen_phi", gen_phi);
    debug_print_vector(std::cerr, "gen_mass", gen_mass);
    debug_print_scalar(std::cerr, std::string(raw_met_prefix) + "_phi", raw_met_phi);
    debug_print_scalar(std::cerr, std::string(raw_met_prefix) + "_pt", raw_met_pt);
    debug_print_vector(std::cerr, "lowpt_rawpt", lowpt_rawpt);
    debug_print_vector(std::cerr, "lowpt_eta", lowpt_eta);
    debug_print_vector(std::cerr, "lowpt_phi", lowpt_phi);
    debug_print_vector(std::cerr, "lowpt_area", lowpt_area);
    debug_print_vector(std::cerr, "lowpt_muon", lowpt_muon);
    debug_print_vector(std::cerr, "lowpt_zero", lowpt_zero);
    debug_print_scalar(std::cerr, "met_dx", met_dx);
    debug_print_scalar(std::cerr, "met_dy", met_dy);
  }
  result.met = bundle.met->produce(
      jet_pt, jet_eta, jet_phi, jet_mass, jet_raw, jet_area, jet_muon, jet_neemef, jet_chemef, jet_jetid, rho, jet_genidx,
      jet_parton, jet_seed, run, gen_pt, gen_eta, gen_phi, gen_mass, raw_met_phi, raw_met_pt, lowpt_rawpt, lowpt_eta, lowpt_phi,
      lowpt_area, lowpt_muon, lowpt_zero, lowpt_zero, met_dx, met_dy, static_cast<unsigned char>(0));
  if (debug_bundle_io) {
    debug_print_met_output(std::cerr, "bundle.met", bundle.met->available(), result.met);
  }
  if (bundle.met_smeared) {
    if (debug_bundle_io) {
      std::cerr << "[NANO_JME_DEBUG_BUNDLE_IO] bundle.met_smeared input\n";
      debug_print_vector(std::cerr, "jet_pt", jet_pt);
      debug_print_vector(std::cerr, "jet_eta", jet_eta);
      debug_print_vector(std::cerr, "jet_phi", jet_phi);
      debug_print_vector(std::cerr, "jet_mass", jet_mass);
      debug_print_vector(std::cerr, "jet_raw", jet_raw);
      debug_print_vector(std::cerr, "jet_area", jet_area);
      debug_print_vector(std::cerr, "jet_muon", jet_muon);
      debug_print_vector(std::cerr, "jet_neEmEF", jet_neemef);
      debug_print_vector(std::cerr, "jet_chEmEF", jet_chemef);
      debug_print_vector(std::cerr, "jet_jetid", jet_jetid);
      debug_print_scalar(std::cerr, "rho", rho);
      debug_print_vector(std::cerr, "jet_genidx", jet_genidx);
      debug_print_vector(std::cerr, "jet_parton", jet_parton);
      debug_print_scalar(std::cerr, "jet_seed", jet_seed);
      debug_print_scalar(std::cerr, "run", run);
      debug_print_vector(std::cerr, "gen_pt", gen_pt);
      debug_print_vector(std::cerr, "gen_eta", gen_eta);
      debug_print_vector(std::cerr, "gen_phi", gen_phi);
      debug_print_vector(std::cerr, "gen_mass", gen_mass);
      debug_print_scalar(std::cerr, std::string(raw_met_prefix) + "_phi", raw_met_phi);
      debug_print_scalar(std::cerr, std::string(raw_met_prefix) + "_pt", raw_met_pt);
      debug_print_vector(std::cerr, "lowpt_rawpt", lowpt_rawpt);
      debug_print_vector(std::cerr, "lowpt_eta", lowpt_eta);
      debug_print_vector(std::cerr, "lowpt_phi", lowpt_phi);
      debug_print_vector(std::cerr, "lowpt_area", lowpt_area);
      debug_print_vector(std::cerr, "lowpt_muon", lowpt_muon);
      debug_print_vector(std::cerr, "lowpt_zero", lowpt_zero);
      debug_print_scalar(std::cerr, "met_dx", met_dx);
      debug_print_scalar(std::cerr, "met_dy", met_dy);
    }
    result.met_smeared = bundle.met_smeared->produce(
        jet_pt, jet_eta, jet_phi, jet_mass, jet_raw, jet_area, jet_muon, jet_neemef, jet_chemef, jet_jetid, rho, jet_genidx,
        jet_parton, jet_seed, run, gen_pt, gen_eta, gen_phi, gen_mass, raw_met_phi, raw_met_pt, lowpt_rawpt, lowpt_eta, lowpt_phi,
        lowpt_area, lowpt_muon, lowpt_zero, lowpt_zero, met_dx, met_dy, static_cast<unsigned char>(0));
    if (debug_bundle_io) {
      debug_print_met_output(std::cerr, "bundle.met_smeared", bundle.met_smeared->available(), result.met_smeared);
    }
  } else {
    result.met_smeared = result.met;
  }

  const auto fatjet_pt = to_rvec(event.vector<float>("FatJet_pt"));
  const auto fatjet_eta = to_rvec(event.vector<float>("FatJet_eta"));
  const auto fatjet_phi = to_rvec(event.vector<float>("FatJet_phi"));
  const auto fatjet_mass = to_rvec(event.vector<float>("FatJet_mass"));
  const auto fatjet_raw = to_rvec(event.vector<float>("FatJet_rawFactor"));
  const auto fatjet_area = to_rvec(event.vector<float>("FatJet_area"));
  const auto fatjet_msd = to_rvec(event.vector<float>("FatJet_msoftdrop"));
  const auto fatjet_sj1 = int_rvec(event, "FatJet_subJetIdx1");
  const auto fatjet_sj2 = int_rvec(event, "FatJet_subJetIdx2");
  const auto fatjet_jetid = jet_id_rvec(fatjet_jets, config_.nano_version);
  const auto fatjet_genidx = is_mc ? int_rvec(event, "FatJet_genJetAK8Idx") : zeros_i(fatjet_pt.size(), -1);
  const auto genfatjet_pt = is_mc ? to_rvec(event.vector<float>("GenJetAK8_pt")) : RVecF{};
  const auto genfatjet_eta = is_mc ? to_rvec(event.vector<float>("GenJetAK8_eta")) : RVecF{};
  const auto genfatjet_phi = is_mc ? to_rvec(event.vector<float>("GenJetAK8_phi")) : RVecF{};
  const auto genfatjet_mass = is_mc ? to_rvec(event.vector<float>("GenJetAK8_mass")) : RVecF{};

  const auto subjet_pt = to_rvec(event.vector<float>("SubJet_pt"));
  const auto subjet_eta = to_rvec(event.vector<float>("SubJet_eta"));
  const auto subjet_phi = to_rvec(event.vector<float>("SubJet_phi"));
  const auto subjet_mass = to_rvec(event.vector<float>("SubJet_mass"));
  const auto subjet_raw = to_rvec(event.vector<float>("SubJet_rawFactor"));
  const auto subjet_area = filled_f(subjet_pt.size(), 0.5f);

  const auto fatjet_seed = rnd_seed(event, fatjet_jets);
  if (debug_bundle_io) {
    std::cerr << "[NANO_JME_DEBUG_BUNDLE_IO] bundle.fatjet_jets input\n";
    debug_print_vector(std::cerr, "fatjet_pt", fatjet_pt);
    debug_print_vector(std::cerr, "fatjet_eta", fatjet_eta);
    debug_print_vector(std::cerr, "fatjet_phi", fatjet_phi);
    debug_print_vector(std::cerr, "fatjet_mass", fatjet_mass);
    debug_print_vector(std::cerr, "fatjet_raw", fatjet_raw);
    debug_print_vector(std::cerr, "fatjet_area", fatjet_area);
    debug_print_vector(std::cerr, "fatjet_msd", fatjet_msd);
    debug_print_vector(std::cerr, "fatjet_sj1", fatjet_sj1);
    debug_print_vector(std::cerr, "fatjet_sj2", fatjet_sj2);
    debug_print_vector(std::cerr, "subjet_pt", subjet_pt);
    debug_print_vector(std::cerr, "subjet_eta", subjet_eta);
    debug_print_vector(std::cerr, "subjet_phi", subjet_phi);
    debug_print_vector(std::cerr, "subjet_mass", subjet_mass);
    debug_print_vector(std::cerr, "subjet_raw", subjet_raw);
    debug_print_vector(std::cerr, "fatjet_jetid", fatjet_jetid);
    debug_print_scalar(std::cerr, "rho", rho);
    debug_print_vector(std::cerr, "fatjet_genidx", fatjet_genidx);
    debug_print_scalar(std::cerr, "fatjet_seed", fatjet_seed);
    debug_print_scalar(std::cerr, "run", run);
    debug_print_vector(std::cerr, "genfatjet_pt", genfatjet_pt);
    debug_print_vector(std::cerr, "genfatjet_eta", genfatjet_eta);
    debug_print_vector(std::cerr, "genfatjet_phi", genfatjet_phi);
    debug_print_vector(std::cerr, "genfatjet_mass", genfatjet_mass);
  }
  result.fatjets = bundle.fatjet_jets->produce(fatjet_pt, fatjet_eta, fatjet_phi, fatjet_mass, fatjet_raw, fatjet_area, fatjet_msd,
                                               fatjet_sj1, fatjet_sj2, subjet_pt, subjet_eta, subjet_phi, subjet_mass, subjet_raw,
                                               fatjet_jetid, rho, fatjet_genidx, fatjet_seed, run, genfatjet_pt, genfatjet_eta,
                                               genfatjet_phi, genfatjet_mass);
  if (debug_bundle_io) {
    debug_print_fatjets_output(std::cerr, "bundle.fatjet_jets", bundle.fatjet_jets->available(), result.fatjets);
  }

  const auto gen_sub_pt = is_mc ? to_rvec(event.vector<float>("SubGenJetAK8_pt")) : RVecF{};
  const auto gen_sub_eta = is_mc ? to_rvec(event.vector<float>("SubGenJetAK8_eta")) : RVecF{};
  const auto gen_sub_phi = is_mc ? to_rvec(event.vector<float>("SubGenJetAK8_phi")) : RVecF{};
  const auto gen_sub_mass = is_mc ? to_rvec(event.vector<float>("SubGenJetAK8_mass")) : RVecF{};
  const auto subjet_seed = rnd_seed(event, subjets);
  const auto subjet_jetid = zeros_i(subjet_pt.size(), 0);
  const auto subjet_genidx = zeros_i(subjet_pt.size(), -1);
  const auto subjet_parton = zeros_i(subjet_pt.size(), 0);
  if (debug_bundle_io) {
    std::cerr << "[NANO_JME_DEBUG_BUNDLE_IO] bundle.subjets input\n";
    debug_print_vector(std::cerr, "subjet_pt", subjet_pt);
    debug_print_vector(std::cerr, "subjet_eta", subjet_eta);
    debug_print_vector(std::cerr, "subjet_phi", subjet_phi);
    debug_print_vector(std::cerr, "subjet_mass", subjet_mass);
    debug_print_vector(std::cerr, "subjet_raw", subjet_raw);
    debug_print_vector(std::cerr, "subjet_area", subjet_area);
    debug_print_vector(std::cerr, "subjet_jetid", subjet_jetid);
    debug_print_scalar(std::cerr, "rho", rho);
    debug_print_vector(std::cerr, "subjet_genidx", subjet_genidx);
    debug_print_vector(std::cerr, "subjet_parton", subjet_parton);
    debug_print_scalar(std::cerr, "subjet_seed", subjet_seed);
    debug_print_scalar(std::cerr, "run", run);
    debug_print_vector(std::cerr, "gen_sub_pt", gen_sub_pt);
    debug_print_vector(std::cerr, "gen_sub_eta", gen_sub_eta);
    debug_print_vector(std::cerr, "gen_sub_phi", gen_sub_phi);
    debug_print_vector(std::cerr, "gen_sub_mass", gen_sub_mass);
  }
  result.subjets =
      bundle.subjets->produce(subjet_pt, subjet_eta, subjet_phi, subjet_mass, subjet_raw, subjet_area, subjet_jetid, rho, subjet_genidx,
                              subjet_parton, subjet_seed, run, gen_sub_pt, gen_sub_eta, gen_sub_phi, gen_sub_mass);
  if (debug_bundle_io) {
    debug_print_jets_output(std::cerr, "bundle.subjets", bundle.subjets->available(), result.subjets);
  }
  return result;
}

void JetMETCorrector::apply_event(Event &event, const JmeEventResult &result, JmeVariation variation) const {
  const auto &bundle = bundle_for_event(event);
  const auto jet_var = jet_variation_index(variation, result.is_mc);
  const auto met_var = met_variation_index(bundle, variation, result.is_mc);
  const auto &met_result =
      (variation == JmeVariation::JerUp || variation == JmeVariation::JerDown) ? result.met_smeared : result.met;

  const auto jet_pt = to_rvec(event.vector<float>("Jet_pt"));
  const auto jet_raw = to_rvec(event.vector<float>("Jet_rawFactor"));
  apply_nominal_jets(event, "Jet", result.ak4_jets.pt(jet_var), result.ak4_jets.mass(jet_var));

  apply_nominal_jets(event, "FatJet", result.fatjets.pt(jet_var), result.fatjets.mass(jet_var));

  apply_nominal_jets(event, "SubJet", result.subjets.pt(jet_var), result.subjets.mass(jet_var));

  event.set("met_pt", static_cast<float>(met_result.pt(met_var)));
  event.set("met_phi", static_cast<float>(met_result.phi(met_var)));
}

void JetMETCorrector::correct_event(Event &event) const {
  JmeVariation variation = JmeVariation::Nominal;
  if (config_.jme_jes == "up") {
    variation = JmeVariation::JesUp;
  } else if (config_.jme_jes == "down") {
    variation = JmeVariation::JesDown;
  } else if (config_.jme_jer == "up") {
    variation = JmeVariation::JerUp;
  } else if (config_.jme_jer == "down") {
    variation = JmeVariation::JerDown;
  } else if (config_.jme_met_unclustered == "up") {
    variation = JmeVariation::MetUp;
  } else if (config_.jme_met_unclustered == "down") {
    variation = JmeVariation::MetDown;
  }
  const auto result = compute_event(event);
  apply_event(event, result, variation);
}

}  // namespace nano
