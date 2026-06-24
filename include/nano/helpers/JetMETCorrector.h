#pragma once

#include "nano/core/Collection.h"
#include "nano/core/Event.h"
#include "nano/helpers/JmeEventResult.h"
#include "nano/helpers/JmeVariation.h"
#include "nano/producers/HeavyFlavBaseProducer.h"

#include "FatJetVariationsCalculator.h"
#include "JetVariationsCalculator.h"
#include "Type1METVariationsCalculator.h"

#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace correction {
class Correction;
}

namespace nano {

class JetMETCorrector {
public:
  explicit JetMETCorrector(const ProducerConfig &config);
  ~JetMETCorrector();

  JmeEventResult compute_event(Event &event) const;
  void apply_event(Event &event, const JmeEventResult &result, JmeVariation variation) const;
  void correct_event(Event &event) const;
  std::int32_t compute_jet_veto_flag(const std::vector<ObjectView> &jets) const;

private:
  struct PayloadPaths {
    std::string payload_dir;
    std::string jet_jerc_json;
    std::string fatjet_jerc_json;
    std::string subjet_jerc_json;
    std::string jer_smear_json;
  };

  struct ObjectSetup {
    std::string payload_subdir;
    std::string jerc_file;
    std::string algo;
    std::string jec_tag_mc;
    std::string jec_tag_data;
    std::string jer_tag_mc;
  };

  struct EraSetup {
    std::string payload_subdir;
    std::vector<std::string> jes_uncertainties;
    std::string met_xy_corr_era;
    ObjectSetup jet;
    ObjectSetup fatjet;
    ObjectSetup subjet;
  };

  struct CalculatorBundle {
    std::unique_ptr<JetVariationsCalculator> ak4_jets;
    std::unique_ptr<FatJetVariationsCalculator> fatjet_jets;
    std::unique_ptr<JetVariationsCalculator> subjets;
    std::unique_ptr<Type1METVariationsCalculator> met;
    std::unique_ptr<Type1METVariationsCalculator> met_smeared;
  };

  static EraSetup build_era_setup(const ProducerConfig &config);
  static PayloadPaths resolve_payload_paths(const ProducerConfig &config, const EraSetup &setup);

  CalculatorBundle make_bundle(bool is_mc) const;
  std::size_t variation_index(const std::vector<std::string> &available, JmeVariation variation, bool is_mc) const;
  const CalculatorBundle &bundle_for_event(const Event &event) const;
  bool should_compute_jet_veto() const;

  ProducerConfig config_;
  EraSetup era_setup_;
  PayloadPaths payload_paths_;
  std::unique_ptr<CalculatorBundle> mc_bundle_;
  std::unique_ptr<CalculatorBundle> data_bundle_;
  std::shared_ptr<const correction::Correction> jet_veto_correction_;
};

}  // namespace nano
