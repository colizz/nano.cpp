#pragma once

#include "nano/producers/HeavyFlavBaseProducer.h"

namespace nano {

class HeavyFlavMuonSampleProducer : public HeavyFlavBaseProducer {
public:
  explicit HeavyFlavMuonSampleProducer(ProducerConfig config);

  void begin_file() override;
  bool analyze(Event &event) override;
  bool analyze_common(Event &event);
  JmeEventResult compute_jme_result(Event &event) const;
  bool analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation);
};

}  // namespace nano
