#pragma once

#include "nano/producers/HeavyFlavBaseProducer.h"

namespace nano {

class HeavyFlavZbbSampleProducer : public HeavyFlavBaseProducer {
public:
  explicit HeavyFlavZbbSampleProducer(ProducerConfig config);

  void begin_file() override;
  bool analyze(Event &event) override;
  bool analyze_common(Event &event) override;
  bool analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) override;

protected:
  std::size_t output_fatjet_count() const override { return 2U; }

private:
  bool require_sv_cut_ = true;
};

}  // namespace nano
