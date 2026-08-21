#pragma once

#include "nano/helpers/MuonCorrection.h"
#include "nano/producers/HeavyFlavBaseProducer.h"

namespace nano {

class HeavyFlavZmmSampleProducer : public HeavyFlavBaseProducer {
public:
  explicit HeavyFlavZmmSampleProducer(ProducerConfig config);

  void begin_file() override;
  bool analyze(Event &event) override;
  bool analyze_common(Event &event) override;
  bool analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) override;

private:
  bool select_muons(Event &event, JmeVariation variation);
  MuonCorrection muon_correction_;
};

}  // namespace nano
