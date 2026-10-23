#include "nano/helpers/NloEWWeightProducer.h"

#include "nano/core/Collection.h"

#include <correction.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <optional>

namespace nano {

namespace {

float get_lhe_v_pt(Event &event) {
  if (!event.is_mc()) {
    return -1.0f;
  }
  if (event.has_physical_branch("LHE_Vpt")) {
    return event.scalar<float>("LHE_Vpt");
  }

  float pt = -1.0f;
  bool found = false;
  for (const auto &particle : event.collection("LHEPart").objects()) {
    const auto id = std::abs(particle.get<std::int32_t>("pdgId"));
    if (id != 23 && id != 24) {
      continue;
    }
    if (found) {
      return -1.0f;
    }
    found = true;
    pt = particle.pt();
  }
  return std::isfinite(pt) && pt >= 0.0f ? pt : -1.0f;
}


// Reject top/Higgs decay products and malformed or cyclic ancestry chains.
bool has_direct_origin(const ObjectView &particle,
                       const std::vector<ObjectView> &genparts) {
  auto mother = particle.get<std::int32_t>("genPartIdxMother");
  for (std::size_t depth = 0; mother >= 0 && depth < genparts.size(); ++depth) {
    if (static_cast<std::size_t>(mother) >= genparts.size()) {
      return false;
    }
    const auto &ancestor = genparts[mother];
    const auto id = std::abs(ancestor.get<std::int32_t>("pdgId"));
    if (id == 6 || id == 25) {
      return false;
    }
    mother = ancestor.get<std::int32_t>("genPartIdxMother");
  }
  return mother == -1;
}

// Find a unique last-copy W/Z from the hard process, excluding top/Higgs decay
// chains. Return no candidate if none or more than one passes these requirements.
std::optional<ObjectView> find_boson(Event &event) {
  const auto genparts = event.collection("GenPart").objects();
  std::optional<ObjectView> candidate;
  constexpr int required_flags = (1 << 8) | (1 << 13); // fromHardProcess, isLastCopy
  for (const auto &particle : genparts) {
    const auto id = std::abs(particle.get<std::int32_t>("pdgId"));
    if (id != 23 && id != 24) {
      continue;
    }
    if ((particle.get<std::int32_t>("statusFlags") & required_flags) != required_flags ||
        !has_direct_origin(particle, genparts)) {
      continue;
    }
    if (candidate) {
      return std::nullopt;
    }
    candidate = particle;
  }
  return candidate;
}

std::shared_ptr<correction::CorrectionSet>
load_corrections(const std::string &path) {
  return std::shared_ptr<correction::CorrectionSet>(
      correction::CorrectionSet::from_file(path).release());
}

} // namespace

NloEWWeightProducer::NloEWWeightProducer(const ProducerConfig &config) {
  const auto &payload_dir = config.channel_options.strings.at("nlo_ewk_payload_dir");
  w_corrections_ = load_corrections(payload_dir + "/vjets_w_nlo_ewk.json.gz");
  z_corrections_ = load_corrections(payload_dir + "/vjets_z_nlo_ewk.json.gz");
}

NloEWWeightProducer::~NloEWWeightProducer() = default;

void NloEWWeightProducer::begin_file(OutputModel &out) const {
  out.branch("lheVpt", -1.0f);
  out.branch("genVpt", -1.0f);
  out.branch("nlo_ew_weight", 1.0f);
  out.branch("nlo_ew_weight_up", 1.0f);
  out.branch("nlo_ew_weight_down", 1.0f);
}

void NloEWWeightProducer::prepare(Event &event) const {
  event.set("lheVpt", get_lhe_v_pt(event));
  event.set("genVpt", -1.0f);
  event.set("nlo_ew_weight", 1.0f);
  event.set("nlo_ew_weight_up", 1.0f);
  event.set("nlo_ew_weight_down", 1.0f);
  if (!event.is_mc()) {
    return;
  }

  const auto boson = find_boson(event);
  if (!boson || !std::isfinite(boson->pt()) || boson->pt() < 0.0f) {
    return;
  }
  const auto pt = boson->pt();
  event.set("genVpt", pt);
  if (pt < 100.0f) {
    return;
  }

  constexpr float maximum_pt = 6500.0f;
  const auto lookup_pt = pt == maximum_pt
                             ? std::nextafter(static_cast<double>(pt), 0.0)
                             : static_cast<double>(pt);
  const auto is_w = std::abs(boson->get<std::int32_t>("pdgId")) == 24;
  const auto correction = is_w ? w_corrections_->at("evj_pTV_EW")
                               : z_corrections_->at("eej_pTV_EW");
  const auto evaluate = [&](const char *variation) {
    return static_cast<float>(
        correction->evaluate({lookup_pt, std::string(variation)}));
  };
  const auto nominal = 1.0f + evaluate("nominal");
  const auto uncertainty = 0.5f * std::abs(nominal - 1.0f);
  event.set("nlo_ew_weight", nominal);
  event.set("nlo_ew_weight_up", nominal + uncertainty);
  event.set("nlo_ew_weight_down", nominal - uncertainty);
}

void NloEWWeightProducer::fill(const Event &event, OutputModel &out) const {
  for (const auto *name : {"lheVpt", "genVpt", "nlo_ew_weight", "nlo_ew_weight_up", "nlo_ew_weight_down"}) {
    out.fill(name, event.get<float>(name));
  }
}

} // namespace nano
