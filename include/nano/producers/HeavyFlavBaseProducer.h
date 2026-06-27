#pragma once

#include "nano/core/Event.h"
#include "nano/core/OutputModel.h"
#include "nano/core/Collection.h"
#include "nano/helpers/JmeEventResult.h"
#include "nano/helpers/JmeVariation.h"

#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace nano {

class JetMETCorrector;
class PuWeightProducer;
class TopPtWeightProducer;
class FatjetGenMatching;

struct JmeObjectConfig {
  std::string payload_subdir;
  std::string jerc_file;
  std::string algo = "AK4PFPuppi";
  std::string jec_tag_mc;
  std::string jec_tag_data;
  std::string jer_tag_mc = "inherit";
};

struct JmeEraConfig {
  std::string payload_subdir;
  std::string jet_jerc_file = "jet_jerc.json.gz";
  std::string fatjet_jerc_file = "fatJet_jerc.json.gz";
  std::string met_xy_corr_era;
  std::vector<std::string> jes_uncertainties;
  JmeObjectConfig jet;
  JmeObjectConfig fatjet;
  JmeObjectConfig subjet;
};

struct PuEraConfig {
  std::string payload_subdir;
  std::string correction_key;
};

struct JetVetoMapEraConfig {
  std::string payload_subdir;
  std::string correction_key;
};

struct BTagConfig {
  std::string branch;
  float loose = 0.0f;
  float medium = 0.0f;
  float tight = 0.0f;
  float xtight = 0.0f;
  float xxtight = 0.0f;
};

struct ChannelOptions {
  std::unordered_map<std::string, bool> bools;
  std::unordered_map<std::string, double> numbers;
  std::unordered_map<std::string, std::string> strings;
};

struct ProducerConfig {
  std::string era = "2024";
  std::string channel;
  std::string nano_version = "v15";
  std::string preselection;
  ChannelOptions channel_options;
  std::vector<std::string> required_triggers;
  std::vector<std::string> read_branches;
  std::unordered_map<std::string, BranchType> nano_branch_types;
  std::vector<std::string> tagger_names;
  BTagConfig btag_config;
  float year_value = 0.0f;
  float lumi_weight = 1.0f;
  std::string jme_payload_dir;
  std::string jme_jer_smear_json;
  std::string jme_jes = "";
  std::string jme_jer = "nominal";
  std::string jme_met_unclustered = "";
  bool jme_smear_met = false;
  std::unordered_map<std::string, JmeEraConfig> jme_eras;
  std::string pu_payload_dir;
  std::unordered_map<std::string, PuEraConfig> pu_eras;
  bool jet_veto_map_enabled = false;
  std::string jet_veto_map_payload_dir;
  std::string jet_veto_map_type = "jetvetomap";
  std::unordered_map<std::string, JetVetoMapEraConfig> jet_veto_map_eras;
  bool include_lhe_weights = false;
};

class HeavyFlavBaseProducer {
public:
  explicit HeavyFlavBaseProducer(ProducerConfig config);
  virtual ~HeavyFlavBaseProducer();

  virtual void begin_file();
  virtual bool analyze(Event &event) = 0;
  virtual bool analyze_common(Event &event) = 0;
  virtual JmeEventResult compute_jme_result(Event &event) const;
  virtual bool analyze_variation(Event &event, const JmeEventResult &jme_result, JmeVariation variation) = 0;

  OutputModel &output() { return out_; }
  const OutputModel &output() const { return out_; }
  static std::vector<BranchSpec> default_schema(const ProducerConfig &config);

protected:
  void prepare_common_objects(Event &event) const;
  void select_leptons(Event &event) const;
  void apply_jme_and_select_jets(Event &event, const JmeEventResult &jme_result, JmeVariation variation) const;
  void load_gen_history(Event &event, std::vector<ObjectView> &fatjets) const;
  void fill_base_event_info(Event &event, JmeVariation variation);
  void fill_fatjet_info(Event &event, const std::vector<ObjectView> &fatjets);

  ProducerConfig config_;
  float jet_cone_size_ = 0.8f;
  std::string fatjet_name_ = "FatJet";
  std::string subjet_name_ = "SubJet";
  std::string genfatjet_name_ = "GenJetAK8";
  std::unique_ptr<JetMETCorrector> jme_corrector_;
  std::unique_ptr<PuWeightProducer> pu_weight_producer_;
  std::unique_ptr<TopPtWeightProducer> top_pt_weight_producer_;
  std::unique_ptr<FatjetGenMatching> fatjet_gen_matching_;
  OutputModel out_;
};

}  // namespace nano
