#include "nano/helpers/TopPtWeightProducer.h"

#include "nano/core/Collection.h"

#include <cmath>
#include <utility>

namespace nano {

namespace {

float clip(float value, float low, float high) {
  return std::max(low, std::min(high, value));
}

bool is_run3_or_later_era(const std::string &era) {
  return era == "2022" || era == "2022EE" || era == "2023" || era == "2023BPix" || era == "2024" || era == "2025" ||
         era == "2026";
}

}  // namespace

TopPtWeightProducer::TopPtWeightProducer(std::string era) : era_(std::move(era)) {}

void TopPtWeightProducer::begin_file(OutputModel &out) const {
  out.branch("topptWeight", 1.0f);
}

void TopPtWeightProducer::fill(Event &event, OutputModel &out) const {
  if (!event.is_mc()) {
    out.fill("topptWeight", 1.0f);
    return;
  }

  auto genparts = event.collection("GenPart").objects();
  std::vector<ObjectView> gen_tops;
  for (auto &gp : genparts) {
    if (std::abs(gp.get<std::int32_t>("pdgId")) != 6) {
      continue;
    }
    if ((gp.get<std::int32_t>("statusFlags") & (1 << 13)) == 0) {
      continue;
    }
    gen_tops.push_back(gp);
  }
  if (gen_tops.size() != 2U) {
    out.fill("topptWeight", 1.0f);
    return;
  }

  // reference: https://twiki.cern.ch/twiki/bin/viewauth/CMS/TopPtReweighting#TOP_PAG_corrections_based_on_the
  // for run 3: https://cms-alcm.web.cern.ch/notes/CMS-AN-2024-019/AN2024_019_v9.pdf
  const auto wgt_nnlo = [](float pt) {
    const auto x = clip(pt, 0.0f, 2000.0f);
    return 0.103f * std::exp(-0.0118f * x) - 0.000134f * x + 0.973f;
  };
  const auto wgt_13_to_13p6 = [](float pt) {
    const auto x = clip(pt, 0.0f, 2000.0f);
    return 0.991f + 0.000075f * x;
  };
  const auto sf = [&](float pt) {
    auto weight = wgt_nnlo(pt);
    if (is_run3_or_later_era(era_)) {
      weight *= wgt_13_to_13p6(pt);
    }
    return weight;
  };
  out.fill("topptWeight", std::sqrt(sf(gen_tops[0].pt()) * sf(gen_tops[1].pt())));
}

}  // namespace nano
