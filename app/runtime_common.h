#pragma once

#include <TDirectory.h>
#include <TEntryList.h>
#include <TFile.h>
#include <TFileMerger.h>
#include <TLeaf.h>
#include <TTree.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nano::runtime {

namespace fs = std::filesystem;

inline std::string trim(std::string value) {
  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

inline std::string strip_comment(std::string line) {
  const auto pos = line.find('#');
  return trim(pos == std::string::npos ? line : line.substr(0, pos));
}

inline std::vector<std::string> split_csv(std::string_view text) {
  std::vector<std::string> out;
  std::stringstream ss{std::string(text)};
  std::string item;
  while (std::getline(ss, item, ',')) {
    item = trim(item);
    if (!item.empty()) {
      out.push_back(item);
    }
  }
  return out;
}

inline std::vector<std::string> split_dot_key(const std::string &key) {
  std::vector<std::string> out;
  std::stringstream ss(key);
  std::string piece;
  while (std::getline(ss, piece, '.')) {
    piece = trim(piece);
    if (!piece.empty()) {
      out.push_back(piece);
    }
  }
  return out;
}

inline bool starts_with(std::string_view value, std::string_view prefix) {
  return value.substr(0, prefix.size()) == prefix;
}

inline std::string normalize_input_path(std::string path) {
  if (starts_with(path, "/store/")) {
    return "root://cms-xrd-global.cern.ch/" + path;
  }
  return path;
}

inline bool is_remote_path(std::string_view path) {
  return starts_with(path, "root://");
}

inline YAML::Node merge_yaml_nodes(const YAML::Node &base, const YAML::Node &overlay) {
  if (!base || base.IsNull()) {
    return YAML::Clone(overlay);
  }
  if (!overlay || overlay.IsNull()) {
    return YAML::Clone(base);
  }
  if (!base.IsMap() || !overlay.IsMap()) {
    return YAML::Clone(overlay);
  }

  YAML::Node merged = YAML::Clone(base);
  for (const auto &item : overlay) {
    const auto key = item.first.as<std::string>();
    if (merged[key] && merged[key].IsMap() && item.second.IsMap()) {
      merged[key] = merge_yaml_nodes(merged[key], item.second);
    } else {
      merged[key] = YAML::Clone(item.second);
    }
  }
  return merged;
}

inline YAML::Node load_config_with_extends(const std::string &path) {
  const auto node = YAML::LoadFile(path);
  if (!node["extends"]) {
    return node;
  }
  const auto parent = fs::path(path).parent_path();
  YAML::Node base = YAML::Node(YAML::NodeType::Map);
  const auto extends = node["extends"];
  if (extends.IsSequence()) {
    for (const auto &entry : extends) {
      const auto base_path = parent / entry.as<std::string>();
      base = merge_yaml_nodes(base, load_config_with_extends(base_path.string()));
    }
  } else if (extends.IsScalar()) {
    const auto base_path = parent / extends.as<std::string>();
    base = load_config_with_extends(base_path.string());
  } else {
    throw std::runtime_error("Config extends must be a string or list of strings: " + path);
  }
  auto overlay = YAML::Clone(node);
  overlay.remove("extends");
  return merge_yaml_nodes(base, overlay);
}

inline YAML::Node parse_override_value(const std::string &value) {
  try {
    return YAML::Load(value);
  } catch (...) {
    return YAML::Node(value);
  }
}

inline void apply_override_path(YAML::Node node, const std::vector<std::string> &pieces, std::size_t index, const YAML::Node &value) {
  if (index + 1 == pieces.size()) {
    node[pieces[index]] = value;
    return;
  }
  if (!node[pieces[index]]) {
    node[pieces[index]] = YAML::Node(YAML::NodeType::Map);
  }
  apply_override_path(node[pieces[index]], pieces, index + 1, value);
}

inline void apply_override(YAML::Node &node, const std::string &key, const std::string &value) {
  const auto pieces = split_dot_key(key);
  if (pieces.empty()) {
    throw std::runtime_error("Empty override key");
  }
  apply_override_path(node, pieces, 0, parse_override_value(value));
}

inline void dump_yaml_file(const YAML::Node &node, const std::string &path) {
  YAML::Emitter out;
  out << node;
  std::ofstream output(path);
  output << out.c_str() << "\n";
}

inline std::string yaml_string(const YAML::Node &node, const std::string &key) {
  if (!node[key]) {
    throw std::runtime_error("Missing config key: " + key);
  }
  return node[key].as<std::string>();
}

inline std::vector<std::string> yaml_string_list(const YAML::Node &node, const std::string &key) {
  std::vector<std::string> out;
  if (!node[key]) {
    return out;
  }
  for (const auto &item : node[key]) {
    out.push_back(item.as<std::string>());
  }
  return out;
}

inline std::map<std::string, std::vector<std::string>> parse_sample_yaml(const std::string &path) {
  const auto root = YAML::LoadFile(path);
  std::map<std::string, std::vector<std::string>> out;
  for (const auto &item : root) {
    const auto key = item.first.as<std::string>();
    for (const auto &entry : item.second) {
      out[key].push_back(entry.as<std::string>());
    }
  }
  return out;
}

inline std::string run_command(const std::string &command) {
  std::array<char, 4096> buffer{};
  std::string output;
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe) {
    throw std::runtime_error("Failed to run command: " + command);
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output.append(buffer.data());
  }
  const auto rc = pclose(pipe);
  if (rc != 0) {
    throw std::runtime_error("Command failed: " + command);
  }
  return output;
}

inline std::vector<std::string> list_remote_dir(const std::string &path) {
  const auto pos = path.find('/', std::string("root://").size());
  if (pos == std::string::npos) {
    return {};
  }
  const auto host = path.substr(0, pos);
  const auto remote_path = path.substr(pos);
  const auto output = run_command("xrdfs " + host + " ls -R " + remote_path);
  std::vector<std::string> files;
  std::stringstream ss(output);
  std::string line;
  while (std::getline(ss, line)) {
    line = trim(line);
    if (line.empty() || !starts_with(line, "/")) {
      continue;
    }
    if (line.size() >= 5 && line.substr(line.size() - 5) == ".root") {
      files.push_back(host + line);
    }
  }
  return files;
}

inline std::vector<std::string> resolve_dataset_entry(const std::string &entry) {
  const auto normalized = normalize_input_path(entry);
  if (starts_with(entry, "/") && std::count(entry.begin(), entry.end(), '/') >= 3 && !fs::exists(entry) && !starts_with(entry, "/store/")) {
    std::vector<std::string> files;
    const auto output = run_command("dasgoclient -query=\"file dataset=" + entry + "\"");
    std::stringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
      line = trim(line);
      if (!line.empty()) {
        files.push_back(normalize_input_path(line));
      }
    }
    return files;
  }

  if (is_remote_path(normalized) && normalized.size() >= 5 && normalized.substr(normalized.size() - 5) != ".root") {
    return list_remote_dir(normalized);
  }

  if (fs::exists(entry) && fs::is_directory(entry)) {
    std::vector<std::string> files;
    for (const auto &dir_entry : fs::recursive_directory_iterator(entry)) {
      if (dir_entry.is_regular_file() && dir_entry.path().extension() == ".root") {
        files.push_back(dir_entry.path().string());
      }
    }
    std::sort(files.begin(), files.end());
    return files;
  }

  if (fs::exists(entry) && fs::is_regular_file(entry) && entry.size() > 5 &&
      (entry.substr(entry.size() - 5) == ".yaml" || entry.substr(entry.size() - 4) == ".txt" || entry.substr(entry.size() - 5) == ".list")) {
    std::ifstream input(entry);
    std::vector<std::string> files;
    std::string line;
    while (std::getline(input, line)) {
      line = strip_comment(line);
      if (!line.empty()) {
        files.push_back(normalize_input_path(line));
      }
    }
    return files;
  }

  return {normalized};
}

inline std::vector<std::string> chunk_join(const std::vector<std::string> &files, std::size_t nfiles_per_job) {
  std::vector<std::string> jobs;
  for (std::size_t i = 0; i < files.size(); i += nfiles_per_job) {
    std::string joined;
    for (std::size_t j = i; j < std::min(files.size(), i + nfiles_per_job); ++j) {
      if (!joined.empty()) {
        joined += ",";
      }
      joined += files[j];
    }
    jobs.push_back(joined);
  }
  return jobs;
}

inline std::vector<Long64_t> build_entry_list(TTree &tree, const std::string &cut, long long max_entries) {
  const auto total = tree.GetEntries();
  std::string effective_cut = cut;
  if (max_entries >= 0) {
    const auto cap_cut = "Entry$<" + std::to_string(max_entries);
    effective_cut = effective_cut.empty() ? cap_cut : "(" + effective_cut + ")&&(" + cap_cut + ")";
  }

  if (effective_cut.empty()) {
    std::vector<Long64_t> entries;
    const auto n = max_entries < 0 ? total : std::min<Long64_t>(total, max_entries);
    entries.reserve(static_cast<std::size_t>(n));
    for (Long64_t i = 0; i < n; ++i) {
      entries.push_back(i);
    }
    return entries;
  }

  const std::string list_name = "entrylist_tmp";
  tree.Draw((">>" + list_name).c_str(), effective_cut.c_str(), "entrylist");
  auto *elist = dynamic_cast<TEntryList *>(gDirectory->Get(list_name.c_str()));
  std::vector<Long64_t> entries;
  if (!elist) {
    return entries;
  }
  const auto n = elist->GetN();
  entries.reserve(static_cast<std::size_t>(n));
  for (Long64_t i = 0; i < n; ++i) {
    entries.push_back(elist->GetEntry(i));
  }
  gDirectory->Delete((list_name + ";*").c_str());
  return entries;
}

inline void copy_selected_tree(TFile &input_file, TFile &output_file, const char *tree_name) {
  auto *input_tree = dynamic_cast<TTree *>(input_file.Get(tree_name));
  if (!input_tree) {
    return;
  }
  output_file.cd();
  auto *output_tree = input_tree->CloneTree(-1, "fast");
  output_tree->Write();
}

inline void merge_root_files(const std::vector<std::string> &inputs, const std::string &output) {
  TFileMerger merger(false, false);
  merger.OutputFile(output.c_str(), "RECREATE");
  for (const auto &input : inputs) {
    merger.AddFile(input.c_str());
  }
  if (!merger.Merge()) {
    throw std::runtime_error("Failed to merge ROOT outputs into " + output);
  }
}

}  // namespace nano::runtime
