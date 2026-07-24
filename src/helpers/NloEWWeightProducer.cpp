#include "nano/helpers/NloEWWeightProducer.h"

#include "nano/core/Collection.h"

#include <TFile.h>
#include <TH1.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace nano {

namespace {

bool starts_with(const std::string &value, const std::string &prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::unique_ptr<TH1> load_histogram(const std::string &path, const std::string &name) {
  std::unique_ptr<TFile> file(TFile::Open(path.c_str(), "READ"));
  if (!file || file->IsZombie()) {
    throw std::runtime_error("Failed to open NLO EW payload: " + path);
  }
  const auto source = dynamic_cast<TH1 *>(file->Get(name.c_str()));
  if (!source) {
    throw std::runtime_error("Missing NLO EW histogram " + name + " in " + path);
  }
  auto histogram = std::unique_ptr<TH1>(dynamic_cast<TH1 *>(source->Clone()));
  if (!histogram) {
    throw std::runtime_error("Failed to clone NLO EW histogram " + name + " from " + path);
  }
  histogram->SetDirectory(nullptr);
  return histogram;
}

std::array<std::unique_ptr<TH1>, 3> load_uncertainty_histograms(
    const std::string &path, const std::vector<std::string> &names) {
  if (names.size() != 3U) {
    throw std::runtime_error("NLO EW correction requires exactly three uncertainty histograms");
  }
  std::array<std::unique_ptr<TH1>, 3> histograms;
  for (std::size_t index = 0; index < histograms.size(); ++index) {
    histograms[index] = load_histogram(path, names[index]);
  }
  return histograms;
}

}  // namespace

NloEWWeightProducer::NloEWWeightProducer(const ProducerConfig &config) {
  const auto sample_it = config.channel_options.strings.find("sample_name");
  const auto sample = sample_it == config.channel_options.strings.end() ? std::string{} : sample_it->second;
  if (starts_with(sample, "Wto") || starts_with(sample, "WJetsTo")) {
    boson_ = Boson::W;
    const auto path = config.nlo_ew.payload_dir + "/" + config.nlo_ew.w_file;
    histogram_ = load_histogram(path, config.nlo_ew.w_histogram);
    uncertainty_histograms_ = load_uncertainty_histograms(path, config.nlo_ew.w_uncertainty_histograms);
  } else if (starts_with(sample, "Zto") || starts_with(sample, "ZJetsTo") || starts_with(sample, "DYto") ||
             starts_with(sample, "DYJetsTo")) {
    boson_ = Boson::Z;
    const auto path = config.nlo_ew.payload_dir + "/" + config.nlo_ew.z_file;
    histogram_ = load_histogram(path, config.nlo_ew.z_histogram);
    uncertainty_histograms_ = load_uncertainty_histograms(path, config.nlo_ew.z_uncertainty_histograms);
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
    if ((boson_ == Boson::W && std::abs(pdg_id) != 24) || (boson_ == Boson::Z && pdg_id != 23)) {
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

  const auto xmax = static_cast<float>(histogram_->GetXaxis()->GetXmax());
  const auto lookup_pt = pt == xmax ? std::nextafter(pt, 0.0f) : pt;
  const auto bin = histogram_->GetXaxis()->FindFixBin(lookup_pt);
  const auto kappa = static_cast<float>(histogram_->GetBinContent(bin));
  const auto nominal = 1.0f + kappa;
  float uncertainty2 = 0.0f;
  for (const auto &histogram : uncertainty_histograms_) {
    const auto delta = static_cast<float>(histogram->GetBinContent(bin));
    uncertainty2 += delta * delta;
  }
  const auto uncertainty = std::sqrt(uncertainty2);
  out.fill("nlo_ew_weight", nominal);
  out.fill("nlo_ew_weight_up", nominal + uncertainty);
  out.fill("nlo_ew_weight_down", nominal - uncertainty);
}

}  // namespace nano
