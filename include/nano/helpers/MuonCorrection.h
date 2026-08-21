#pragma once

#include "nano/core/Collection.h"
#include "nano/helpers/JmeVariation.h"
#include "nano/producers/HeavyFlavBaseProducer.h"

#include "MuonVariationsCalculator.h"

#include <memory>
#include <string>
#include <vector>

namespace correction {
class CorrectionSet;
}

namespace nano {

struct MuonSFResult {
  float nominal = 1.0f;
  float stat_up = 1.0f;
  float stat_down = 1.0f;
  float syst_up = 1.0f;
  float syst_down = 1.0f;
};

class MuonCorrection {
public:
  explicit MuonCorrection(const ProducerConfig &config);

  MuonVariationsCalculator::result_t produce(Event &event, const std::vector<ObjectView> &muons) const;
  void apply(const MuonVariationsCalculator::result_t &result, JmeVariation variation,
             std::vector<ObjectView> &muons) const;
  MuonSFResult scale_factor(const std::vector<ObjectView> &muons, const std::string &correction_key,
                            float minimum_pt) const;

private:
  MuonVariationsCalculator mc_calculator_;
  MuonVariationsCalculator data_calculator_;
  std::shared_ptr<correction::CorrectionSet> scale_factors_;
};

}  // namespace nano
