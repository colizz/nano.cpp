#pragma once

#include "nano/core/Collection.h"
#include "nano/helpers/JmeVariation.h"
#include "nano/helpers/MuonTriggerSF.h"
#include "nano/producers/HeavyFlavBaseProducer.h"

#include "MuonVariationsCalculator.h"

#include <memory>
#include <string>
#include <vector>

namespace correction {
class CorrectionSet;
}

namespace nano {

class MuonCorrector {
public:
  explicit MuonCorrector(const ProducerConfig &config);

  MuonVariationsCalculator::result_t compute(Event &event, const std::vector<ObjectView> &muons) const;
  void apply(const MuonVariationsCalculator::result_t &result, JmeVariation variation,
             std::vector<ObjectView> &muons) const;
  MuonSFResult scale_factor(const std::vector<ObjectView> &muons, const std::string &correction_key,
                            float minimum_pt) const;
  MuonSFResult trigger_or_scale_factor(const std::vector<ObjectView> &muons,
                                      const std::string &correction_key, float minimum_pt) const;
  MuonSFResult high_pt_scale_factor(const std::vector<ObjectView> &muons,
                                    const std::string &correction_key, float minimum_pt) const;

private:
  std::unique_ptr<MuonVariationsCalculator> mc_calculator_;
  std::unique_ptr<MuonVariationsCalculator> data_calculator_;
  std::unique_ptr<MuonTriggerSF> trigger_sf_;
  std::shared_ptr<correction::CorrectionSet> scale_factors_;
  std::shared_ptr<correction::CorrectionSet> high_pt_scale_factors_;
};

}  // namespace nano
