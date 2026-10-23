#pragma once

#include "nano/producers/HeavyFlavBaseProducer.h"
#include "nano/helpers/TopPtWeightProducer.h"

namespace nano {

class HeavyFlavTrigSFProducer : public HeavyFlavBaseProducer {
public:
  explicit HeavyFlavTrigSFProducer(ProducerConfig config);

  void begin_file() override;
  bool analyze(Event &event) override;
  bool analyze_common(Event &event) override;
  bool analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) override;

private:
  void select_trigger_jets(Event &event) const;
  TopPtWeightProducer top_pt_weight_producer_;
};

}  // namespace nano
