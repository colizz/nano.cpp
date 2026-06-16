#include "nano/producers/HeavyFlavBaseProducer.h"

#include "nano/core/Collection.h"
#include "nano/core/Helpers.h"
#include "nano/helpers/FatjetGenMatching.h"
#include "nano/helpers/JetMETCorrector.h"
#include "nano/helpers/PuWeightProducer.h"
#include "nano/helpers/TopPtWeightProducer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>

namespace nano {

namespace {

template <typename T>
std::vector<ObjectView> filter_objects(const std::vector<ObjectView> &in, T &&predicate) {
  std::vector<ObjectView> out;
  for (const auto &obj : in) {
    if (predicate(obj)) {
      out.push_back(obj);
    }
  }
  return out;
}

std::vector<ObjectView> sort_by_pt(std::vector<ObjectView> objects) {
  std::sort(objects.begin(), objects.end(), [](const auto &a, const auto &b) { return a.pt() > b.pt(); });
  return objects;
}

float safe_scalar_float(const Event &event, std::string_view branch_name, float fallback) {
  return event.schema().find(branch_name) ? event.scalar<float>(branch_name) : fallback;
}

std::vector<ObjectView> subjets_for(const ObjectView &fatjet, Event &event, std::string_view subjet_name) {
  const auto subjets = event.collection(subjet_name).objects();
  std::vector<ObjectView> out;
  const auto idx1 = safe_object_int(fatjet, "subJetIdx1", -1);
  const auto idx2 = safe_object_int(fatjet, "subJetIdx2", -1);
  if (idx1 >= 0 && static_cast<std::size_t>(idx1) < subjets.size()) {
    out.push_back(subjets[static_cast<std::size_t>(idx1)]);
  }
  if (idx2 >= 0 && static_cast<std::size_t>(idx2) < subjets.size()) {
    out.push_back(subjets[static_cast<std::size_t>(idx2)]);
  }
  return out;
}

}  // namespace

HeavyFlavBaseProducer::HeavyFlavBaseProducer(ProducerConfig config) : config_(std::move(config)) {
  jme_corrector_ = std::make_unique<JetMETCorrector>(config_);
  pu_weight_producer_ = std::make_unique<PuWeightProducer>(config_);
  top_pt_weight_producer_ = std::make_unique<TopPtWeightProducer>(config_.era);
  fatjet_gen_matching_ = std::make_unique<FatjetGenMatching>();
}

HeavyFlavBaseProducer::~HeavyFlavBaseProducer() = default;

// Define the shared output branches owned by the base producer. Channel
// producers call this once per output file before the event loop, then add
// their channel-specific branches on top.
void HeavyFlavBaseProducer::begin_file() {
  out_.branch("run", std::uint32_t{0});
  out_.branch("luminosityBlock", std::uint32_t{0});
  out_.branch("event", std::uint64_t{0});
  out_.branch("year", 0.0f);
  out_.branch("lumiwgt", 0.0f);
  out_.branch("jetR", jet_cone_size_);
  out_.branch("passmetfilters", false);
  out_.branch("l1PreFiringWeight", 1.0f);
  out_.branch("l1PreFiringWeightUp", 1.0f);
  out_.branch("l1PreFiringWeightDown", 1.0f);
  out_.branch("nlep", std::int32_t{0});
  out_.branch("ht", 0.0f);
  out_.branch("met", 0.0f);
  out_.branch("metphi", 0.0f);
  out_.branch("jetVetoFlag", std::int32_t{-99});
  out_.branch("genWeight", 1.0f);
  if (config_.include_lhe_weights) {
    out_.branch("LHEScaleWeight", std::vector<float>{});
  }
  pu_weight_producer_->begin_file(out_);
  top_pt_weight_producer_->begin_file(out_);

  out_.branch("fj_1_is_qualified", false);
  for (const auto *name : {"fj_1_pt", "fj_1_eta", "fj_1_phi", "fj_1_mass", "fj_1_sdmass",
                           "fj_1_tau1", "fj_1_tau2", "fj_1_tau3",
                           "fj_1_tau4", "fj_1_deltaR_sj12", "fj_1_sj1_pt", "fj_1_sj1_eta", "fj_1_sj1_phi",
                           "fj_1_sj1_mass", "fj_1_sj1_btagdeepcsv", "fj_1_sj2_pt", "fj_1_sj2_eta", "fj_1_sj2_phi",
                           "fj_1_sj2_mass", "fj_1_sj2_btagdeepcsv"}) {
    out_.branch(name, 0.0f);
  }
  for (const auto &tagger : config_.tagger_names) {
    out_.branch("fj_1_" + tagger, -99.0f);
  }

  for (const auto *name : {"fj_1_genfj_nbhadrons",   "fj_1_genfj_nchadrons",   "fj_1_genfj_partonflavour",
                           "fj_1_nbhadrons",         "fj_1_nchadrons",         "fj_1_partonflavour",
                           "fj_1_sj1_nbhadrons",     "fj_1_sj1_nchadrons",     "fj_1_sj1_partonflavour",
                           "fj_1_sj2_nbhadrons",     "fj_1_sj2_nchadrons",     "fj_1_sj2_partonflavour",
                           "fj_1_H_decay",           "fj_1_Z_decay",           "fj_1_W_decay",
                           "fj_1_T_Wq_max_pdgId",    "fj_1_T_Wq_min_pdgId"}) {
    out_.branch(name, std::int32_t{0});
  }

  for (const auto *name : {"fj_1_dr_H",       "fj_1_dr_H_daus",  "fj_1_H_pt",         "fj_1_dr_Z",      "fj_1_dr_Z_daus",
                           "fj_1_Z_pt",       "fj_1_dr_W",       "fj_1_dr_W_daus",    "fj_1_W_pt",      "fj_1_dr_T",
                           "fj_1_dr_T_b",     "fj_1_dr_T_Wq_max","fj_1_dr_T_Wq_min",  "fj_1_T_pt"}) {
    out_.branch(name, 0.0f);
  }
}

// Build the explicit input schema from the runtime card. nano_run has already
// normalized read_branches and resolved each branch type from the NanoAOD
// catalogue, so this function only de-duplicates and converts that manifest
// into NanoReader binding specs.
std::vector<BranchSpec> HeavyFlavBaseProducer::default_schema(const ProducerConfig &config) {
  std::vector<BranchSpec> specs;
  std::set<std::string> seen;
  const auto add_branch = [&](const std::string &name, bool optional) {
    if (!seen.insert(name).second) {
      return;
    }
    const auto it = config.nano_branch_types.find(name);
    if (it == config.nano_branch_types.end()) {
      throw std::runtime_error("Branch " + name + " is not listed in nano_branches for " + config.nano_version);
    }
    specs.push_back({name, it->second, optional});
  };

  for (const auto &branch : config.read_branches) {
    add_branch(branch, true);
  }
  return specs;
}

// Prepare shared variation-independent objects for downstream channel logic.
// Loose leptons are used for jet cleaning, and fatjet gen matching is attached
// once by object index because it depends on eta/phi rather than JME-varied
// pt/mass.
void HeavyFlavBaseProducer::prepare_common_objects(Event &event) const {
  select_leptons(event);
  auto fatjets = event.collection(fatjet_name_).objects();
  load_gen_history(event, fatjets);
}

// Select loose electrons and muons used for lepton counting and jet cleaning.
// The selected objects are stored on the event as "looseLeptons" for later
// channel and base-producer steps.
void HeavyFlavBaseProducer::select_leptons(Event &event) const {
  auto electrons = event.collection("Electron").objects();
  const auto electron_id = config_.nano_version == "v9" ? "mvaFall17V2noIso_WP90" : "mvaNoIso_WP90";
  std::vector<ObjectView> loose_leptons;
  for (auto &el : electrons) {
    if (el.pt() > 10.0f && std::abs(el.eta()) < 2.5f && std::abs(el.get<float>("dxy")) < 0.05f &&
        std::abs(el.get<float>("dz")) < 0.2f && el.get<bool>(electron_id) && el.get<float>("miniPFRelIso_all") < 0.4f) {
      loose_leptons.push_back(el);
    }
  }

  auto muons = event.collection("Muon").objects();
  for (auto &mu : muons) {
    if (mu.pt() > 10.0f && std::abs(mu.eta()) < 2.4f && std::abs(mu.get<float>("dxy")) < 0.05f &&
        std::abs(mu.get<float>("dz")) < 0.2f && mu.get<bool>("looseId") && mu.get<float>("miniPFRelIso_all") < 0.4f) {
      loose_leptons.push_back(mu);
    }
  }

  event.set("looseLeptons", sort_by_pt(std::move(loose_leptons)));
}

// Convenience path for a single nominal JME pass. Multi-variation running uses
// compute_jme() once and then calls apply_jme_and_select_jets() for each output
// variation.
void HeavyFlavBaseProducer::correct_jets_and_met(Event &event) const {
  const auto jme_result = compute_jme(event);
  apply_jme_and_select_jets(event, jme_result, JmeVariation::Nominal);
}

JmeEventResult HeavyFlavBaseProducer::compute_jme(Event &event) const {
  return jme_corrector_->compute_event(event);
}

// Apply one JME variation to Jet/FatJet/SubJet/MET, then build the cleaned jet
// collections used by downstream selection and filling. Fatjet gen-matching
// extras are attached to the event by object index in analyze_common(), so
// freshly constructed FatJet handles can still read those common attributes
// while using the current variation's corrected pt/mass extras.
void HeavyFlavBaseProducer::apply_jme_and_select_jets(Event &event, const JmeEventResult &jme_result, JmeVariation variation) const {
  jme_corrector_->apply_event(event, jme_result, variation);

  auto fatjets = event.collection(fatjet_name_).objects();
  auto ak4jets = event.collection("Jet").objects();
  event.set("jetVetoFlag", jme_corrector_->compute_jet_veto_flag(ak4jets));

  for (auto &fj : fatjets) {
    fj.set("idx", static_cast<std::int32_t>(fj.index()));
    fj.set("is_qualified", true);
    const auto subjets = subjets_for(fj, event, subjet_name_);
    fj.set("subjets", subjets);
    LorentzVector subjet_sum;
    for (const auto &sj : subjets) {
      subjet_sum += sj.p4();
    }
    fj.set("msoftdrop", static_cast<float>(subjet_sum.M()));
  }

  fatjets = filter_objects(sort_by_pt(std::move(fatjets)), [&](const auto &fj) {
    return fj.pt() > 200.0f && std::abs(fj.eta()) < 2.4f && pass_jet_id(fj, config_.nano_version, false);
  });
  ak4jets = filter_objects(sort_by_pt(std::move(ak4jets)), [&](const auto &jet) {
    return jet.pt() > 25.0f && std::abs(jet.eta()) < 2.4f && pass_jet_id(jet, config_.nano_version, true);
  });

  const auto &loose = event.get<std::vector<ObjectView>>("looseLeptons");
  auto clean_fatjets = std::vector<ObjectView>{};
  for (auto &fj : fatjets) {
    auto [idx, dr] = closest_index(fj, loose);
    if (idx < 0 || dr >= jet_cone_size_) {
      clean_fatjets.push_back(fj);
    }
  }

  auto clean_ak4jets = std::vector<ObjectView>{};
  for (auto &jet : ak4jets) {
    auto [idx, dr] = closest_index(jet, loose);
    if (idx < 0 || dr >= 0.4f) {
      clean_ak4jets.push_back(jet);
    }
  }

  float ht = 0.0f;
  for (const auto &jet : clean_ak4jets) {
    ht += jet.pt();
  }

  event.set("fatjets", clean_fatjets);
  event.set("ak4jets", clean_ak4jets);
  event.set("ht", ht);
}

// Attach generator-level matching information to selected fatjets. The helper
// computes Higgs/W/Z/top matching variables and stores them as derived fatjet
// attributes that fill_fatjet_info() later writes to output branches.
void HeavyFlavBaseProducer::load_gen_history(Event &event, std::vector<ObjectView> &fatjets) const {
  fatjet_gen_matching_->process(event, fatjets);
}

// Reset and fill event-level output shared by all heavy-flavour channels:
// identifiers, era/lumi labels, MET filters, L1 prefiring, lepton count, HT,
// corrected MET, generator weight, optional LHE weights, PU weights, and top-pt
// weights.
void HeavyFlavBaseProducer::fill_base_event_info(Event &event) {
  out_.reset();
  out_.fill("run", event.scalar<std::uint32_t>("run"));
  out_.fill("luminosityBlock", event.scalar<std::uint32_t>("luminosityBlock"));
  out_.fill("event", event.scalar<std::uint64_t>("event"));
  out_.fill("jetR", jet_cone_size_);
  out_.fill("year", config_.year_value);
  out_.fill("lumiwgt", config_.lumi_weight);

  bool met_filters = safe_bool(event, "Flag_goodVertices") && safe_bool(event, "Flag_globalSuperTightHalo2016Filter") &&
                     safe_bool(event, "Flag_EcalDeadCellTriggerPrimitiveFilter") && safe_bool(event, "Flag_BadPFMuonFilter") &&
                     safe_bool(event, "Flag_BadPFMuonDzFilter") && safe_bool(event, "Flag_eeBadScFilter");
  if (config_.era == "2016APV" || config_.era == "2016" || config_.era == "2017" || config_.era == "2018") {
    met_filters = met_filters && safe_bool(event, "Flag_HBHENoiseFilter") && safe_bool(event, "Flag_HBHENoiseIsoFilter");
  }
  if (config_.era == "2017" || config_.era == "2018" || config_.era == "2022" || config_.era == "2022EE" || config_.era == "2023" ||
      config_.era == "2023BPix" || config_.era == "2024") {
    met_filters = met_filters && safe_bool(event, "Flag_ecalBadCalibFilter");
  }
  if (config_.era == "2022" || config_.era == "2022EE" || config_.era == "2023" || config_.era == "2023BPix" || config_.era == "2024") {
    met_filters = met_filters && safe_bool(event, "Flag_hfNoisyHitsFilter");
  }
  out_.fill("passmetfilters", met_filters);
  const bool use_l1_prefiring = event.is_mc() && (config_.era == "2016APV" || config_.era == "2016" || config_.era == "2017");
  out_.fill("l1PreFiringWeight", use_l1_prefiring ? safe_scalar_float(event, "L1PreFiringWeight_Nom", 1.0f) : 1.0f);
  out_.fill("l1PreFiringWeightUp", use_l1_prefiring ? safe_scalar_float(event, "L1PreFiringWeight_Up", 1.0f) : 1.0f);
  out_.fill("l1PreFiringWeightDown", use_l1_prefiring ? safe_scalar_float(event, "L1PreFiringWeight_Dn", 1.0f) : 1.0f);
  out_.fill("nlep", static_cast<std::int32_t>(event.get<std::vector<ObjectView>>("looseLeptons").size()));
  out_.fill("ht", event.get<float>("ht"));
  out_.fill("met", event.get<float>("met_pt"));
  out_.fill("metphi", event.get<float>("met_phi"));
  out_.fill("jetVetoFlag", event.has("jetVetoFlag") ? event.get<std::int32_t>("jetVetoFlag") : std::int32_t{-99});
  out_.fill("genWeight", event.is_mc() ? event.scalar<float>("genWeight") : 1.0f);
  if (config_.include_lhe_weights) {
    out_.fill("LHEScaleWeight",
              event.has_physical_branch("LHEScaleWeight") ? event.vector<float>("LHEScaleWeight") : std::vector<float>{});
  }
  pu_weight_producer_->fill(event, out_);
  top_pt_weight_producer_->fill(event, out_);
}

// Fill the leading selected fatjet block. Values come from the JME-corrected
// fatjet and its linked corrected subjets; tagger branches are driven by
// stored_tagger_names, and gen-matching branches use attributes attached by
// load_gen_history().
void HeavyFlavBaseProducer::fill_fatjet_info(Event &event, const std::vector<ObjectView> &fatjets) {
  if (fatjets.empty()) {
    return;
  }
  const auto &fj = fatjets.front();
  const auto subjets = fj.extra<std::vector<ObjectView>>("subjets");

  out_.fill("fj_1_is_qualified", fj.get<bool>("is_qualified"));
  out_.fill("fj_1_pt", fj.pt());
  out_.fill("fj_1_eta", fj.eta());
  out_.fill("fj_1_phi", fj.phi());
  out_.fill("fj_1_mass", fj.mass());
  out_.fill("fj_1_sdmass", fj.get<float>("msoftdrop"));
  out_.fill("fj_1_tau1", safe_object_float(fj, "tau1", 0.0f));
  out_.fill("fj_1_tau2", safe_object_float(fj, "tau2", 0.0f));
  out_.fill("fj_1_tau3", safe_object_float(fj, "tau3", 0.0f));
  out_.fill("fj_1_tau4", safe_object_float(fj, "tau4", 0.0f));
  for (const auto &tagger : config_.tagger_names) {
    out_.fill("fj_1_" + tagger, safe_object_float(fj, tagger, -99.0f));
  }
  if (!subjets.empty()) {
    out_.fill("fj_1_sj1_pt", subjets[0].pt());
    out_.fill("fj_1_sj1_eta", subjets[0].eta());
    out_.fill("fj_1_sj1_phi", subjets[0].phi());
    out_.fill("fj_1_sj1_mass", subjets[0].mass());
    out_.fill("fj_1_sj1_btagdeepcsv", safe_object_float(subjets[0], "btagDeepB", -1.0f));
    out_.fill("fj_1_sj1_nbhadrons", safe_object_int(subjets[0], "nBHadrons", -1));
    out_.fill("fj_1_sj1_nchadrons", safe_object_int(subjets[0], "nCHadrons", -1));
    out_.fill("fj_1_sj1_partonflavour", safe_object_int(subjets[0], "partonFlavour", -1));
  }
  if (subjets.size() > 1U) {
    out_.fill("fj_1_deltaR_sj12", delta_r(subjets[0], subjets[1]));
    out_.fill("fj_1_sj2_pt", subjets[1].pt());
    out_.fill("fj_1_sj2_eta", subjets[1].eta());
    out_.fill("fj_1_sj2_phi", subjets[1].phi());
    out_.fill("fj_1_sj2_mass", subjets[1].mass());
    out_.fill("fj_1_sj2_btagdeepcsv", safe_object_float(subjets[1], "btagDeepB", -1.0f));
    out_.fill("fj_1_sj2_nbhadrons", safe_object_int(subjets[1], "nBHadrons", -1));
    out_.fill("fj_1_sj2_nchadrons", safe_object_int(subjets[1], "nCHadrons", -1));
    out_.fill("fj_1_sj2_partonflavour", safe_object_int(subjets[1], "partonFlavour", -1));
  } else {
    out_.fill("fj_1_deltaR_sj12", 99.0f);
  }

  const auto gen_fatjets = event.collection(genfatjet_name_).objects();
  const auto gen_idx = safe_object_int(fj, "genJetAK8Idx", -1);
  if (gen_idx >= 0 && static_cast<std::size_t>(gen_idx) < gen_fatjets.size()) {
    const auto &gen_fj = gen_fatjets[static_cast<std::size_t>(gen_idx)];
    out_.fill("fj_1_genfj_nbhadrons", safe_object_int(gen_fj, "nBHadrons", -1));
    out_.fill("fj_1_genfj_nchadrons", safe_object_int(gen_fj, "nCHadrons", -1));
    out_.fill("fj_1_genfj_partonflavour", safe_object_int(gen_fj, "partonFlavour", -1));
  } else {
    out_.fill("fj_1_genfj_nbhadrons", std::int32_t{-1});
    out_.fill("fj_1_genfj_nchadrons", std::int32_t{-1});
    out_.fill("fj_1_genfj_partonflavour", std::int32_t{-1});
  }

  out_.fill("fj_1_nbhadrons", safe_object_int(fj, "nBHadrons", -1));
  out_.fill("fj_1_nchadrons", safe_object_int(fj, "nCHadrons", -1));
  out_.fill("fj_1_partonflavour", safe_object_int(fj, "partonFlavour", -1));
  out_.fill("fj_1_dr_H", safe_object_float(fj, "dr_H", 99.0f));
  out_.fill("fj_1_dr_H_daus", safe_object_float(fj, "dr_H_daus", 99.0f));
  out_.fill("fj_1_H_pt", safe_object_float(fj, "H_pt", -1.0f));
  out_.fill("fj_1_H_decay", safe_object_int(fj, "H_decay", 0));
  out_.fill("fj_1_dr_Z", safe_object_float(fj, "dr_Z", 99.0f));
  out_.fill("fj_1_dr_Z_daus", safe_object_float(fj, "dr_Z_daus", 99.0f));
  out_.fill("fj_1_Z_pt", safe_object_float(fj, "Z_pt", -1.0f));
  out_.fill("fj_1_Z_decay", safe_object_int(fj, "Z_decay", 0));
  out_.fill("fj_1_dr_W", safe_object_float(fj, "dr_W", 99.0f));
  out_.fill("fj_1_dr_W_daus", safe_object_float(fj, "dr_W_daus", 99.0f));
  out_.fill("fj_1_W_pt", safe_object_float(fj, "W_pt", -1.0f));
  out_.fill("fj_1_W_decay", safe_object_int(fj, "W_decay", 0));
  out_.fill("fj_1_dr_T", safe_object_float(fj, "dr_T", 99.0f));
  out_.fill("fj_1_dr_T_b", safe_object_float(fj, "dr_T_b", 99.0f));
  out_.fill("fj_1_dr_T_Wq_max", safe_object_float(fj, "dr_T_Wq_max", 99.0f));
  out_.fill("fj_1_dr_T_Wq_min", safe_object_float(fj, "dr_T_Wq_min", 99.0f));
  out_.fill("fj_1_T_Wq_max_pdgId", safe_object_int(fj, "T_Wq_max_pdgId", 0));
  out_.fill("fj_1_T_Wq_min_pdgId", safe_object_int(fj, "T_Wq_min_pdgId", 0));
  out_.fill("fj_1_T_pt", safe_object_float(fj, "T_pt", -1.0f));
}

}  // namespace nano
