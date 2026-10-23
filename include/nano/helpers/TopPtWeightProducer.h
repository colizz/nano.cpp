#pragma once

#include "nano/core/Event.h"
#include "nano/core/OutputModel.h"

#include <string>

namespace nano {

class TopPtWeightProducer {
public:
  explicit TopPtWeightProducer(std::string era);

  void begin_file(OutputModel &out) const;
  // Compute once per event before applying reconstruction variations.
  void prepare(Event &event) const;
  void fill(const Event &event, OutputModel &out) const;

private:
  std::string era_;
};

}  // namespace nano
