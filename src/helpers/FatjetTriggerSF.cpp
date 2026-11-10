#include "nano/helpers/FatjetTriggerSF.h"

#include <correction.h>
#include <cmath>

namespace nano {

/*
 * Fatjet trigger SF usage (zbb v15)
 * - Evaluate one event-level trigger-OR SF, using the highest finite-pt probe
 *   from all cleaned AK8 jets before Xbb ranking/truncation, for each JME state.
 * - Coordinates are corrected pt and rawMass * globalParT3_massCorrX2p;
 *   subjet soft-drop mass is recorded only as a diagnostic.
 * - Require 200 <= pt < 1500 GeV, 0 <= mass < 500 GeV and a valid payload bin.
 *   Empty/disabled payloads or invalid probes/bins return unit weights with
 *   valid=false and status=0. Payload loading/lookup errors propagate.
 * - Nominal/stat_up/stat_down are full alternate event weights, applied once
 *   to MC by downstream weighting, separately from genWeight. Data keeps unit
 *   weights while retaining the evaluated probe and validity/status metadata.
 */

FatjetTriggerSF::FatjetTriggerSF(const std::string &filename, const std::string &name) {
  if (filename.empty()) return;
  auto payload = correction::CorrectionSet::from_file(filename);
  sf_ = payload->at(name);
  valid_ = payload->at(name + "_valid");
  status_ = payload->at(name + "_status");
}

FatjetTriggerSFValues FatjetTriggerSF::evaluate(const std::vector<TriggerSFProbe> &probes, bool is_mc) const {
  FatjetTriggerSFValues result;
  for (const auto &probe : probes) {
    if (std::isfinite(probe.pt) && probe.pt > result.pt) {
      result.pt = probe.pt;
      result.mass = probe.mass;
      result.msd = probe.msd;
    }
  }
  if (!sf_ || !std::isfinite(result.mass) || result.pt < 200 || result.pt >= 1500 ||
      result.mass < 0 || result.mass >= 500) return result;
  const std::vector<correction::Variable::Type> coordinates = {
      static_cast<double>(result.pt), static_cast<double>(result.mass)};
  if (valid_->evaluate(coordinates) < 0.5) return result;
  const auto evaluate = [&](const char *variation) {
    return sf_->evaluate({static_cast<double>(result.pt), static_cast<double>(result.mass), std::string(variation)});
  };
  const auto nominal = evaluate("nominal"), up = evaluate("stat_up"), down = evaluate("stat_down");
  if (!std::isfinite(nominal) || !std::isfinite(up) || !std::isfinite(down) ||
      nominal < 0 || up < 0 || down < 0) return result;
  result.valid = true;
  result.status = static_cast<int>(status_->evaluate(coordinates));
  if (is_mc) {
    result.nominal = nominal;
    result.stat_up = up;
    result.stat_down = down;
  }
  return result;
}

}  // namespace nano
