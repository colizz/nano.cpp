#pragma once

#include "nano/producers/HeavyFlavBaseProducer.h"
#include "nano/helpers/TopPtWeightProducer.h"

namespace nano {

class HeavyFlavMuonSampleProducer : public HeavyFlavBaseProducer {
public:
  explicit HeavyFlavMuonSampleProducer(ProducerConfig config);

  void begin_file() override;
  bool analyze(Event &event) override;
  bool analyze_common(Event &event) override;
  bool analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) override;

private:
  TopPtWeightProducer top_pt_weight_producer_;
};

}  // namespace nano
