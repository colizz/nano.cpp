#include "nano/core/Event.h"
#include "nano/io/NanoReader.h"
#include "nano/io/RootOutputFile.h"
#include "nano/producers/HeavyFlavMuonSampleProducer.h"

#include "runtime_common.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

struct CliOptions {
  std::string input_files;
  std::string output_file;
  std::string tree_name = "Events";
  long long num_events = -1;
  std::string channel = "muon";
  std::string config_file;
  std::string variations;
  bool run_data = false;
  std::unordered_map<std::string, std::string> overrides;
};

CliOptions parse_args(int argc, char **argv) {
  CliOptions opts;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto need_value = [&](const char *name) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error(std::string("Missing value for ") + name);
      }
      return argv[++i];
    };
    if (arg == "--input-files") {
      opts.input_files = need_value("--input-files");
    } else if (arg == "--output-file") {
      opts.output_file = need_value("--output-file");
    } else if (arg == "--tree-name") {
      opts.tree_name = need_value("--tree-name");
    } else if (arg == "--num-events") {
      opts.num_events = std::stoll(need_value("--num-events"));
    } else if (arg == "--channel") {
      opts.channel = need_value("--channel");
    } else if (arg == "--config") {
      opts.config_file = need_value("--config");
    } else if (arg == "--variations") {
      opts.variations = need_value("--variations");
    } else if (arg == "--run-data") {
      opts.run_data = true;
    } else if (arg == "--set") {
      const auto kv = need_value("--set");
      const auto pos = kv.find('=');
      if (pos == std::string::npos) {
        throw std::runtime_error("--set expects key=value");
      }
      opts.overrides[kv.substr(0, pos)] = kv.substr(pos + 1);
    } else {
      throw std::runtime_error("Unknown argument: " + arg);
    }
  }

  if (opts.input_files.empty() || opts.output_file.empty() || opts.config_file.empty()) {
    throw std::runtime_error("Usage: nano_run --input-files <files> --output-file <out.root> --config <card.yaml> [--channel muon] [--num-events -1] [--run-data] [--variations nominal,jes_up,...] [--set key=value]");
  }
  return opts;
}

void validate_data_variations(const CliOptions &cli) {
  if (!cli.run_data || cli.variations.empty() || cli.variations == "nominal") {
    return;
  }
  throw std::runtime_error("--run-data does not support JME variations. If --variations is used with --run-data, it must be the single value 'nominal'; otherwise omit --variations.");
}

nano::ProducerConfig make_config(const YAML::Node &settings, const std::string &channel) {
  const auto validate_era_nano_version = [](const std::string &era, const std::string &nano_version) {
    if (nano_version != "v9" && nano_version != "v12" && nano_version != "v15") {
      throw std::runtime_error("nano_version must be one of: v9, v12, v15. Got: " + nano_version);
    }

    const auto run2 = std::set<std::string>{"2016APV", "2016", "2017", "2018"};
    const auto run3_early = std::set<std::string>{"2022", "2022EE", "2023", "2023BPix"};
    const auto run3_late = std::set<std::string>{"2024", "2025", "2026"};
    const auto allowed = (run2.count(era) != 0U && (nano_version == "v9" || nano_version == "v15")) ||
                         (run3_early.count(era) != 0U && (nano_version == "v12" || nano_version == "v15")) ||
                         (run3_late.count(era) != 0U && nano_version == "v15");
    if (!allowed) {
      throw std::runtime_error("Unsupported era/nano_version pair: era=" + era + ", nano_version=" + nano_version);
    }
  };

  const auto parse_branch_type = [](const std::string &type, const std::string &branch_name) {
    if (type == "bool") {
      return nano::BranchType::kBool;
    }
    if (type == "int32" || type == "int16" || type == "int8") {
      return nano::BranchType::kInt32;
    }
    if (type == "uint32" || type == "uint16" || type == "uint8") {
      return nano::BranchType::kUInt32;
    }
    if (type == "uint64") {
      return nano::BranchType::kUInt64;
    }
    if (type == "float") {
      return nano::BranchType::kFloat;
    }
    if (type == "vec_bool") {
      return nano::BranchType::kVecBool;
    }
    if (type == "vec_uint8") {
      return nano::BranchType::kVecUInt8;
    }
    if (type == "vec_uint16") {
      return nano::BranchType::kVecUInt16;
    }
    if (type == "vec_int16") {
      return nano::BranchType::kVecInt16;
    }
    if (type == "vec_int32" || type == "vec_int8") {
      return nano::BranchType::kVecInt32;
    }
    if (type == "vec_float") {
      return nano::BranchType::kVecFloat;
    }
    throw std::runtime_error("Unsupported NanoAOD branch type '" + type + "' for branch " + branch_name);
  };

  nano::ProducerConfig config;
  config.channel = channel;
  config.era = settings["era"].as<std::string>();
  config.nano_version = settings["nano_version"].as<std::string>();
  validate_era_nano_version(config.era, config.nano_version);
  config.selection = settings["selections"][channel].as<std::string>();
  config.required_triggers = nano::runtime::yaml_string_list(settings, "required_triggers");

  const auto catalogue = settings["nano_branches"][config.nano_version]["trees"]["Events"]["branches"];
  if (!catalogue) {
    throw std::runtime_error("Missing NanoAOD branch catalogue for " + config.nano_version);
  }
  config.read_branches = nano::runtime::yaml_string_list(settings, "read_branches");
  if (config.read_branches.empty()) {
    throw std::runtime_error("Missing or empty read_branches list in config");
  }
  if (settings["output"] && settings["output"]["include_lhe_weights"]) {
    config.include_lhe_weights = settings["output"]["include_lhe_weights"].as<bool>();
  }

  // read_branches safety checks:
  // - Runtime cards should explicitly list every physical NanoAOD branch the
  //   channel reads.
  // - required_triggers, stored_tagger_names, and optional LHEScaleWeight can
  //   imply extra physical branches; auto-add them for backward compatibility,
  //   but warn so the card can be made explicit.
  // - If LHEScaleWeight is listed while output.include_lhe_weights is disabled,
  //   remove it so a stale read_branches entry does not silently read an unused
  //   large vector branch.
  if (!config.include_lhe_weights) {
    const auto old_size = config.read_branches.size();
    config.read_branches.erase(std::remove(config.read_branches.begin(), config.read_branches.end(), "LHEScaleWeight"),
                               config.read_branches.end());
    if (config.read_branches.size() != old_size) {
      std::cerr << "Warning: removing LHEScaleWeight from read_branches because output.include_lhe_weights is false.\n";
    }
  }
  if (!settings["stored_tagger_names"]) {
    throw std::runtime_error("Missing stored_tagger_names list in config");
  }
  for (const auto &item : settings["stored_tagger_names"]) {
    config.tagger_names.push_back(item.as<std::string>());
  }
  std::set<std::string> seen(config.read_branches.begin(), config.read_branches.end());
  for (const auto &trigger : config.required_triggers) {
    if (seen.insert(trigger).second) {
      std::cerr << "Warning: adding missing branch " << trigger
                << " to read_branches from required_triggers. Please list it explicitly in read_branches.\n";
      config.read_branches.push_back(trigger);
    }
    config.nano_branch_types[trigger] = nano::BranchType::kBool;
  }
  for (const auto &tagger : config.tagger_names) {
    const auto branch_name = "FatJet_" + tagger;
    if (seen.insert(branch_name).second) {
      std::cerr << "Warning: adding missing branch " << branch_name
                << " to read_branches from stored_tagger_names. Please list it explicitly in read_branches.\n";
      config.read_branches.push_back(branch_name);
    }
  }
  if (config.include_lhe_weights && seen.insert("LHEScaleWeight").second) {
    std::cerr << "Warning: adding missing branch LHEScaleWeight to read_branches because output.include_lhe_weights is true. "
                 "Please list it explicitly in read_branches.\n";
    config.read_branches.push_back("LHEScaleWeight");
  }
  for (const auto &branch_name : config.read_branches) {
    if (config.nano_branch_types.count(branch_name) != 0U) {
      continue;
    }
    const auto branch_node = catalogue[branch_name];
    if (!branch_node) {
      throw std::runtime_error("Branch " + branch_name + " is not listed in nano_branches for " + config.nano_version);
    }
    config.nano_branch_types[branch_name] = parse_branch_type(branch_node["type"].as<std::string>(), branch_name);
  }
  const auto btag_node = settings["btag"][config.nano_version][config.era];
  if (!btag_node) {
    throw std::runtime_error("Missing btag config for nano version " + config.nano_version + " and era " + config.era);
  }
  config.btag_config.branch = btag_node["branch"].as<std::string>();
  config.btag_config.loose = btag_node["loose"] ? btag_node["loose"].as<float>() : 0.0f;
  config.btag_config.medium = btag_node["medium"] ? btag_node["medium"].as<float>() : 0.0f;
  config.btag_config.tight = btag_node["tight"] ? btag_node["tight"].as<float>() : 0.0f;
  config.btag_config.xtight = btag_node["xtight"] ? btag_node["xtight"].as<float>() : 0.0f;
  config.btag_config.xxtight = btag_node["xxtight"] ? btag_node["xxtight"].as<float>() : 0.0f;
  config.year_value = settings["year_values"][config.era].as<float>();
  config.lumi_weight = settings["lumi_values"][config.era].as<float>();
  config.jme_payload_dir = settings["jec"]["payload_dir"].as<std::string>();
  config.jme_jer_smear_json = settings["jec"]["jer_smear_json"].as<std::string>();
  if (settings["jec"]["systematics"]) {
    const auto syst = settings["jec"]["systematics"];
    config.jme_jes = syst["jes"] ? syst["jes"].as<std::string>() : "";
    config.jme_jer = syst["jer"] ? syst["jer"].as<std::string>() : "nominal";
    config.jme_met_unclustered = syst["met_unclustered"] ? syst["met_unclustered"].as<std::string>() : "";
    config.jme_smear_met = syst["smear_met"] ? syst["smear_met"].as<bool>() : false;
  }
  auto parse_jme_object = [](const YAML::Node &node, std::string default_algo, std::string default_jerc_file) {
    nano::JmeObjectConfig object;
    object.payload_subdir = node["payload_subdir"] ? node["payload_subdir"].as<std::string>() : "";
    object.jerc_file = node["jerc_file"] ? node["jerc_file"].as<std::string>() : std::move(default_jerc_file);
    object.algo = node["algo"] ? node["algo"].as<std::string>() : std::move(default_algo);
    object.jec_tag_mc = node["jec_tag_mc"] ? node["jec_tag_mc"].as<std::string>() : "";
    object.jec_tag_data = node["jec_tag_data"] ? node["jec_tag_data"].as<std::string>() : "";
    object.jer_tag_mc = node["jer_tag_mc"] ? node["jer_tag_mc"].as<std::string>() : "inherit";
    return object;
  };
  const auto campaigns = settings["jec"]["campaigns"] ? settings["jec"]["campaigns"] : settings["jec"]["eras"];
  for (const auto &item : campaigns) {
    nano::JmeEraConfig era_cfg;
    era_cfg.payload_subdir = item.second["payload_subdir"].as<std::string>();
    era_cfg.jet_jerc_file = item.second["jet_jerc_file"] ? item.second["jet_jerc_file"].as<std::string>() : "jet_jerc.json.gz";
    era_cfg.fatjet_jerc_file = item.second["fatjet_jerc_file"] ? item.second["fatjet_jerc_file"].as<std::string>() : "fatJet_jerc.json.gz";
    era_cfg.met_xy_corr_era = item.second["met_xy_corr_era"].as<std::string>();
    if (item.second["jes_uncertainties"]) {
      for (const auto &unc : item.second["jes_uncertainties"]) {
        era_cfg.jes_uncertainties.push_back(unc.as<std::string>());
      }
    }
    era_cfg.jet = parse_jme_object(item.second["jet"], "AK4PFPuppi", era_cfg.jet_jerc_file);
    era_cfg.fatjet = parse_jme_object(item.second["fatjet"], "AK8PFPuppi", era_cfg.fatjet_jerc_file);
    era_cfg.subjet = parse_jme_object(item.second["subjet"], "AK4PFPuppi", era_cfg.jet_jerc_file);
    config.jme_eras[item.first.as<std::string>()] = era_cfg;
  }
  config.pu_payload_dir = settings["pu"]["payload_dir"].as<std::string>();
  for (const auto &item : settings["pu"]["eras"]) {
    config.pu_eras[item.first.as<std::string>()] = {
        item.second["payload_subdir"].as<std::string>(),
        item.second["correction_key"].as<std::string>(),
    };
  }
  if (settings["jet_veto_map"]) {
    const auto node = settings["jet_veto_map"];
    config.jet_veto_map_enabled = node["enabled"] ? node["enabled"].as<bool>() : false;
    config.jet_veto_map_payload_dir = node["payload_dir"] ? node["payload_dir"].as<std::string>() : config.jme_payload_dir;
    config.jet_veto_map_type = node["type"] ? node["type"].as<std::string>() : "jetvetomap";
    if (node["campaigns"]) {
      for (const auto &item : node["campaigns"]) {
        config.jet_veto_map_eras[item.first.as<std::string>()] = {
            item.second["payload_subdir"].as<std::string>(),
            item.second["correction"].as<std::string>(),
        };
      }
    }
  }
  return config;
}

std::unique_ptr<nano::HeavyFlavBaseProducer> make_producer(const nano::ProducerConfig &config) {
  if (config.channel == "muon") {
    return std::make_unique<nano::HeavyFlavMuonSampleProducer>(config);
  }
  throw std::runtime_error("Unsupported channel: " + config.channel);
}

std::string variation_output_path(const std::string &output_file, nano::JmeVariation variation) {
  const fs::path path(output_file);
  const auto suffix = "_" + std::string(nano::variation_name(variation));
  const auto extension = path.has_extension() ? path.extension().string() : std::string{};
  const auto stem = path.has_extension() ? path.stem().string() : path.filename().string();
  const auto parent = path.parent_path();
  return (parent / (stem + suffix + extension)).string();
}

std::string data_lumi_mask_path(const YAML::Node &settings, const nano::ProducerConfig &config) {
  const auto masks = settings["data_lumi_masks"];
  if (!masks || !masks[config.era]) {
    throw std::runtime_error("Missing data_lumi_masks entry for era " + config.era + " in config");
  }
  return masks[config.era].as<std::string>();
}

void process_one_file(const std::string &input_file, const std::string &output_file, const CliOptions &cli,
                      const YAML::Node &settings) {
  auto input = std::unique_ptr<TFile>(TFile::Open(input_file.c_str(), "READ"));
  if (!input || input->IsZombie()) {
    throw std::runtime_error("Failed to open input file: " + input_file);
  }
  auto *tree = dynamic_cast<TTree *>(input->Get(cli.tree_name.c_str()));
  if (!tree) {
    throw std::runtime_error("Missing tree " + cli.tree_name + " in " + input_file);
  }

  const auto config = make_config(settings, cli.channel);
  const auto lumi_mask = cli.run_data ? std::make_unique<nano::runtime::LumiMask>(nano::runtime::LumiMask::from_file(data_lumi_mask_path(settings, config)))
                                      : nullptr;
  auto producer = make_producer(config);
  producer->begin_file();

  nano::RootOutputFile output(output_file);
  output.book_events(producer->output());

  const auto entry_list = nano::runtime::build_entry_list(*tree, config.selection, cli.num_events, lumi_mask.get());
  nano::NanoReader reader(*tree, nano::BranchSchema(nano::HeavyFlavBaseProducer::default_schema(config)));

  std::size_t accepted = 0;
  std::set<nano::runtime::RunLumi> selected_lumis;
  for (const auto entry : entry_list) {
    nano::Event event(reader, static_cast<std::size_t>(entry));
    if (!producer->analyze(event)) {
      continue;
    }
    output.fill_event(producer->output());
    selected_lumis.insert({event.scalar<std::uint32_t>("run"), event.scalar<std::uint32_t>("luminosityBlock")});
    ++accepted;
  }

  nano::runtime::copy_filtered_runs_tree(*input, output.file(), selected_lumis);
  nano::runtime::copy_filtered_luminosity_blocks_tree(*input, output.file(), selected_lumis);
  output.write();

  std::cout << "input=" << input_file << " processed=" << entry_list.size() << " accepted=" << accepted << " output=" << output_file << "\n";
}

std::vector<std::string> process_one_file_variations(const std::string &input_file, const std::string &output_file, const CliOptions &cli,
                                                     const YAML::Node &settings,
                                                     const std::vector<nano::JmeVariation> &variations) {
  auto input = std::unique_ptr<TFile>(TFile::Open(input_file.c_str(), "READ"));
  if (!input || input->IsZombie()) {
    throw std::runtime_error("Failed to open input file: " + input_file);
  }
  auto *tree = dynamic_cast<TTree *>(input->Get(cli.tree_name.c_str()));
  if (!tree) {
    throw std::runtime_error("Missing tree " + cli.tree_name + " in " + input_file);
  }

  const auto config = make_config(settings, cli.channel);
  const auto lumi_mask = cli.run_data ? std::make_unique<nano::runtime::LumiMask>(nano::runtime::LumiMask::from_file(data_lumi_mask_path(settings, config)))
                                      : nullptr;
  auto producer_base = make_producer(config);
  auto *producer = dynamic_cast<nano::HeavyFlavMuonSampleProducer *>(producer_base.get());
  if (!producer) {
    throw std::runtime_error("Multi-variation running is currently implemented for the muon channel");
  }
  producer->begin_file();

  struct VariationOutput {
    nano::JmeVariation variation;
    std::string file_name;
    std::unique_ptr<nano::RootOutputFile> output;
    std::size_t accepted = 0;
    std::set<nano::runtime::RunLumi> selected_lumis;
  };
  std::vector<VariationOutput> outputs;
  outputs.reserve(variations.size());
  for (const auto variation : variations) {
    auto path = variation_output_path(output_file, variation);
    if (const auto parent = fs::path(path).parent_path(); !parent.empty()) {
      fs::create_directories(parent);
    }
    auto output = std::make_unique<nano::RootOutputFile>(path);
    output->book_events(producer->output());
    outputs.push_back({variation, std::move(path), std::move(output), 0U});
  }

  const auto entry_list = nano::runtime::build_entry_list(*tree, config.selection, cli.num_events, lumi_mask.get());
  nano::NanoReader reader(*tree, nano::BranchSchema(nano::HeavyFlavBaseProducer::default_schema(config)));

  for (const auto entry : entry_list) {
    nano::Event event(reader, static_cast<std::size_t>(entry));
    if (!producer->analyze_common(event)) {
      continue;
    }
    const auto jme_result = producer->compute_jme_result(event);
    for (auto &item : outputs) {
      if (!producer->analyze_variation(event, jme_result, item.variation)) {
        continue;
      }
      item.output->fill_event(producer->output());
      item.selected_lumis.insert({event.scalar<std::uint32_t>("run"), event.scalar<std::uint32_t>("luminosityBlock")});
      ++item.accepted;
    }
  }

  std::vector<std::string> output_files;
  for (auto &item : outputs) {
    nano::runtime::copy_filtered_runs_tree(*input, item.output->file(), item.selected_lumis);
    nano::runtime::copy_filtered_luminosity_blocks_tree(*input, item.output->file(), item.selected_lumis);
    item.output->write();
    std::cout << "input=" << input_file << " processed=" << entry_list.size() << " accepted=" << item.accepted
              << " variation=" << nano::variation_name(item.variation) << " output=" << item.file_name << "\n";
    output_files.push_back(item.file_name);
  }
  return output_files;
}

}  // namespace

int main(int argc, char **argv) {
  try {
    const auto cli = parse_args(argc, argv);
    validate_data_variations(cli);
    auto settings = nano::runtime::load_config_with_extends(cli.config_file);
    for (const auto &[key, value] : cli.overrides) {
      nano::runtime::apply_override(settings, key, value);
    }

    auto inputs = nano::runtime::split_csv(cli.input_files);
    for (auto &input : inputs) {
      input = nano::runtime::normalize_input_path(input);
    }

    const auto variations = cli.variations.empty() ? std::vector<nano::JmeVariation>{} : nano::parse_jme_variation_list(cli.variations);
    if (!variations.empty()) {
      if (inputs.size() == 1U) {
        process_one_file_variations(inputs.front(), cli.output_file, cli, settings, variations);
        return 0;
      }

      const auto temp_dir = fs::path("run") / ("pieces_" + std::to_string(::getpid()));
      fs::create_directories(temp_dir);
      std::unordered_map<std::string, std::vector<std::string>> piece_outputs;
      for (const auto variation : variations) {
        piece_outputs[std::string(nano::variation_name(variation))] = {};
      }
      for (std::size_t i = 0; i < inputs.size(); ++i) {
        const auto piece_base = (temp_dir / ("piece_" + std::to_string(i) + ".root")).string();
        const auto outputs = process_one_file_variations(inputs[i], piece_base, cli, settings, variations);
        for (std::size_t j = 0; j < variations.size(); ++j) {
          piece_outputs[std::string(nano::variation_name(variations[j]))].push_back(outputs[j]);
        }
      }
      for (const auto variation : variations) {
        const auto name = std::string(nano::variation_name(variation));
        const auto final_output = variation_output_path(cli.output_file, variation);
        nano::runtime::merge_root_files(piece_outputs.at(name), final_output);
        std::cout << "merged=" << final_output << " variation=" << name << " pieces=" << piece_outputs.at(name).size() << "\n";
      }
      return 0;
    }

    if (inputs.size() == 1U) {
      process_one_file(inputs.front(), cli.output_file, cli, settings);
      return 0;
    }

    const auto temp_dir = fs::path("run") / ("pieces_" + std::to_string(::getpid()));
    fs::create_directories(temp_dir);
    std::vector<std::string> piece_outputs;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      const auto piece = (temp_dir / ("piece_" + std::to_string(i) + ".root")).string();
      process_one_file(inputs[i], piece, cli, settings);
      piece_outputs.push_back(piece);
    }
    nano::runtime::merge_root_files(piece_outputs, cli.output_file);
    std::cout << "merged=" << cli.output_file << " pieces=" << piece_outputs.size() << "\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "nano_run failed: " << ex.what() << "\n";
    return 1;
  }
}
