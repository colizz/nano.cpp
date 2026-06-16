#pragma once

#include "nano/core/BranchSchema.h"
#include "nano/core/NanoTypes.h"

#include <TFile.h>
#include <TTree.h>
#include <TTreeReader.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nano {

class NanoReader {
public:
  // The reader binds only the explicitly declared branches and exposes them
  // through a stable typed interface to the analysis layer.
  NanoReader(std::string ntuple_name, std::string file_name, BranchSchema schema);
  NanoReader(TTree &tree, BranchSchema schema);

  void load(std::size_t entry);
  std::size_t entries() const;

  const BranchSchema &schema() const { return schema_; }
  bool has_physical_branch(std::string_view branch_name) const;
  const FieldPtr &field(std::string_view branch_name) const;

  struct BranchLoader {
    virtual ~BranchLoader() = default;
    virtual void load() = 0;
  };

private:
  std::string ntuple_name_;
  std::string file_name_;
  BranchSchema schema_;
  std::unique_ptr<TFile> file_;
  TTree *tree_ = nullptr;
  std::unique_ptr<TTreeReader> reader_;
  std::unordered_map<std::string, FieldPtr> fields_;
  std::unordered_set<std::string> physical_branches_;
  std::vector<std::unique_ptr<BranchLoader>> loaders_;

  void bind_branches();
};

}  // namespace nano
