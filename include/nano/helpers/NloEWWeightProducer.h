#pragma once

#include "nano/core/Event.h"
#include "nano/core/OutputModel.h"
#include "nano/producers/HeavyFlavBaseProducer.h"

#include <array>
#include <memory>

class TH1;

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
  std::unique_ptr<TH1> histogram_;
  std::array<std::unique_ptr<TH1>, 3> uncertainty_histograms_;
};

}  // namespace nano
