#pragma once

#include "nano/core/Event.h"
#include "nano/core/OutputModel.h"
#include "nano/producers/HeavyFlavBaseProducer.h"

#include <memory>

namespace correction {
class CorrectionSet;
}

namespace nano {

class NloEWWeightProducer {
public:
  explicit NloEWWeightProducer(const ProducerConfig &config);
  ~NloEWWeightProducer();

  void begin_file(OutputModel &out) const;
  // Compute once per event before applying reconstruction variations.
  void prepare(Event &event) const;
  void fill(const Event &event, OutputModel &out) const;

private:
  std::shared_ptr<correction::CorrectionSet> w_corrections_;
  std::shared_ptr<correction::CorrectionSet> z_corrections_;
};

}  // namespace nano
