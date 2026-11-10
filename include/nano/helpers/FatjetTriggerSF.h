#pragma once

#include <memory>
#include <string>
#include <vector>

namespace correction { class Correction; }

namespace nano {

struct TriggerSFProbe {
  float pt;
  float mass;
  float msd;
};

struct FatjetTriggerSFValues {
  float nominal = 1.f, stat_up = 1.f, stat_down = 1.f;
  float pt = -1.f, mass = -1.f, msd = -1.f;
  bool valid = false;
  int status = 0;
};

// Event-level correction: the highest-pT cleaned AK8 defines the coordinates.
class FatjetTriggerSF {
public:
  explicit FatjetTriggerSF(const std::string &filename,
                           const std::string &name = "zbb_2024_or_trigger_sf_gptmass_x2p");
  FatjetTriggerSFValues evaluate(const std::vector<TriggerSFProbe> &probes, bool is_mc) const;
private:
  std::shared_ptr<const correction::Correction> sf_, valid_, status_;
};

}  // namespace nano
