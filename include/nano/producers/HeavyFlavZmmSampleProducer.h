#pragma once

#include "nano/helpers/MuonCorrector.h"
#include "nano/producers/HeavyFlavBaseProducer.h"
#include "nano/helpers/NloEWWeightProducer.h"

namespace nano {

class HeavyFlavZmmSampleProducer : public HeavyFlavBaseProducer {
public:
  explicit HeavyFlavZmmSampleProducer(ProducerConfig config);

  void begin_file() override;
  bool analyze(Event &event) override;
  bool analyze_common(Event &event) override;
  bool analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) override;

private:
  NloEWWeightProducer nlo_ew_weight_producer_;
  void select_muons(Event &event, JmeVariation variation);
  MuonCorrector muon_corrector_;
};

}  // namespace nano
