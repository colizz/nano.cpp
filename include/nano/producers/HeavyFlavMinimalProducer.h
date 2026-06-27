#pragma once

#include "nano/producers/HeavyFlavBaseProducer.h"

namespace nano {

class HeavyFlavMinimalProducer : public HeavyFlavBaseProducer {
public:
  explicit HeavyFlavMinimalProducer(ProducerConfig config);

  bool analyze(Event &event) override;
  bool analyze_common(Event &event) override;
  bool analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) override;
};

}  // namespace nano
