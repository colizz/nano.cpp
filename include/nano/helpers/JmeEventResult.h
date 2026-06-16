#pragma once

#include "FatJetVariationsCalculator.h"
#include "JetVariationsCalculator.h"
#include "Type1METVariationsCalculator.h"

namespace nano {

struct JmeEventResult {
  bool is_mc = false;
  JetVariationsCalculator::result_t ak4_jets;
  FatJetVariationsCalculator::result_t fatjets;
  JetVariationsCalculator::result_t subjets;
  Type1METVariationsCalculator::result_t met;
  Type1METVariationsCalculator::result_t met_smeared;
};

}  // namespace nano
