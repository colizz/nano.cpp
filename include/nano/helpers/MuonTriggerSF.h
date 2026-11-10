#pragma once
#include "nano/core/Collection.h"
#include <memory>
#include <string>
#include <vector>
namespace correction { class Correction; class CorrectionSet; }
namespace nano {
struct MuonSFResult {
  float nominal = 1.f, stat_up = 1.f, stat_down = 1.f, syst_up = 1.f, syst_down = 1.f;
  float effstat_up = 1.f, effstat_down = 1.f;
  bool valid = true;
  int source = 0; // 0: no eligible muon/Data; 1: official efficiencies; 2: derived MC efficiency.
};
// Independent of momentum calibration; supports Run 2 SFs with absolute eta.
class MuonTriggerSF {
public:
  MuonTriggerSF(const std::string &era, const std::string &sf_file,
                const std::string &efficiency_file = "");
  static std::string correction_name(const std::string &era);
  MuonSFResult evaluate(const std::vector<ObjectView> &muons, float minimum_pt = 52.f,
                        const std::string &key = "") const;
private:
  std::string era_, key_;
  bool absolute_eta_ = false, official_efficiencies_ = false;
  std::shared_ptr<const correction::Correction> sf_, efficiency_, efficiency_valid_;
};
}
