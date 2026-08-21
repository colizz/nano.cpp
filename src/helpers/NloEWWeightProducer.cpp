#include "nano/helpers/NloEWWeightProducer.h"

#include "nano/core/Collection.h"

#include <correction.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace nano {

namespace {

bool starts_with(const std::string &value, const std::string &prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::shared_ptr<correction::CorrectionSet>
load_corrections(const std::string &path) {
  return std::shared_ptr<correction::CorrectionSet>(
      correction::CorrectionSet::from_file(path).release());
}

} // namespace

NloEWWeightProducer::NloEWWeightProducer(const ProducerConfig &config) {
  const auto sample_it = config.channel_options.strings.find("sample_name");
  const auto sample = sample_it == config.channel_options.strings.end()
                          ? std::string{}
                          : sample_it->second;
  if (starts_with(sample, "Wto") || starts_with(sample, "WJetsTo")) {
    boson_ = Boson::W;
    corrections_ = load_corrections(config.nlo_ew.payload_dir + "/" +
                                    config.nlo_ew.w_file);
    correction_key_ = config.nlo_ew.w_correction;
  } else if (starts_with(sample, "Zto") || starts_with(sample, "ZJetsTo") ||
             starts_with(sample, "DYto") || starts_with(sample, "DYJetsTo")) {
    boson_ = Boson::Z;
    corrections_ = load_corrections(config.nlo_ew.payload_dir + "/" +
                                    config.nlo_ew.z_file);
    correction_key_ = config.nlo_ew.z_correction;
  }
}

NloEWWeightProducer::~NloEWWeightProducer() = default;

void NloEWWeightProducer::begin_file(OutputModel &out) const {
  out.branch("nlo_ew_weight", 1.0f);
  out.branch("nlo_ew_weight_up", 1.0f);
  out.branch("nlo_ew_weight_down", 1.0f);
}

void NloEWWeightProducer::fill(Event &event, OutputModel &out) const {
  if (!event.is_mc() || boson_ == Boson::None) {
    return;
  }

  float fallback_pt = -1.0f;
  float last_copy_pt = -1.0f;
  for (const auto &particle : event.collection("GenPart").objects()) {
    const auto pdg_id = particle.get<std::int32_t>("pdgId");
    if ((boson_ == Boson::W && std::abs(pdg_id) != 24) ||
        (boson_ == Boson::Z && pdg_id != 23)) {
      continue;
    }
    fallback_pt = std::max(fallback_pt, particle.pt());
    if ((particle.get<std::int32_t>("statusFlags") & (1 << 13)) != 0) {
      last_copy_pt = std::max(last_copy_pt, particle.pt());
    }
  }

  const auto pt = last_copy_pt >= 0.0f ? last_copy_pt : fallback_pt;
  if (pt < 100.0f) {
    return;
  }

  // Preserve the legacy ROOT lookup, where x == xmax belongs to the final bin.
  constexpr float maximum_pt = 6500.0f;
  const auto lookup_pt = pt == maximum_pt
                             ? std::nextafter(static_cast<double>(pt), 0.0)
                             : static_cast<double>(pt);
  const auto correction = corrections_->at(correction_key_);
  const auto evaluate = [&](const char *variation) {
    return static_cast<float>(
        correction->evaluate({lookup_pt, std::string(variation)}));
  };
  const auto nominal = 1.0f + evaluate("nominal");
  const auto uncertainty = 0.5f * std::abs(nominal - 1.0f);
  out.fill("nlo_ew_weight", nominal);
  out.fill("nlo_ew_weight_up", nominal + uncertainty);
  out.fill("nlo_ew_weight_down", nominal - uncertainty);
}

} // namespace nano
