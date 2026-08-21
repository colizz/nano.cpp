#pragma once

#include "nano/core/Event.h"
#include "nano/core/OutputModel.h"
#include "nano/producers/HeavyFlavBaseProducer.h"

#include <memory>
#include <string>

namespace correction {
class CorrectionSet;
}

namespace nano {

class NloEWWeightProducer {
public:
  explicit NloEWWeightProducer(const ProducerConfig &config);
  ~NloEWWeightProducer();

  void begin_file(OutputModel &out) const;
  void fill(Event &event, OutputModel &out) const;

private:
  enum class Boson { None, W, Z };

  Boson boson_ = Boson::None;
  std::shared_ptr<correction::CorrectionSet> corrections_;
  std::string correction_key_;
};

}  // namespace nano
