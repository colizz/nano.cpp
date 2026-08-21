#pragma once

#include "nano/core/OutputModel.h"

#include <TFile.h>
#include <TTree.h>

#include <memory>
#include <string>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace nano {

class RootOutputFile {
public:
  using BranchStorage = std::variant<bool, std::int32_t, std::uint32_t, std::uint64_t, float, std::vector<float>>;

  explicit RootOutputFile(std::string file_name);
  ~RootOutputFile();

  void book_events(const OutputModel &model, std::string_view tree_name = "Events");
  void set_branch_title(std::string_view branch_name, std::string_view title);
  void fill_event(const OutputModel &model);
  void write();

  TFile &file() { return *file_; }

private:
  std::string file_name_;
  std::unique_ptr<TFile> file_;
  TTree *events_tree_ = nullptr;
  std::unordered_map<std::string, BranchStorage> storage_;
};

}  // namespace nano
