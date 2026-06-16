#include "nano/io/NanoReader.h"

#include <TBranch.h>
#include <TTreeReaderArray.h>
#include <TTreeReaderValue.h>

#include <stdexcept>

namespace nano {

namespace {

template <typename T>
FieldPtr make_scalar_field() {
  return std::make_shared<T>();
}

template <typename T>
FieldPtr make_vector_field() {
  return std::make_shared<std::vector<T>>();
}

template <typename RootT, typename DestT>
class ScalarLoader : public NanoReader::BranchLoader {
public:
  ScalarLoader(TTreeReader &reader, std::string branch_name, std::shared_ptr<DestT> dest)
      : value_(reader, branch_name.c_str()), dest_(std::move(dest)) {}

  void load() override { *dest_ = static_cast<DestT>(*value_); }

private:
  TTreeReaderValue<RootT> value_;
  std::shared_ptr<DestT> dest_;
};

template <typename RootT, typename DestT>
class ArrayLoader : public NanoReader::BranchLoader {
public:
  ArrayLoader(TTreeReader &reader, std::string branch_name, std::shared_ptr<std::vector<DestT>> dest)
      : array_(reader, branch_name.c_str()), dest_(std::move(dest)) {}

  void load() override {
    dest_->clear();
    dest_->reserve(array_.GetSize());
    for (auto value : array_) {
      dest_->push_back(static_cast<DestT>(value));
    }
  }

private:
  TTreeReaderArray<RootT> array_;
  std::shared_ptr<std::vector<DestT>> dest_;
};

}  // namespace

NanoReader::NanoReader(std::string ntuple_name, std::string file_name, BranchSchema schema)
    : ntuple_name_(std::move(ntuple_name)), file_name_(std::move(file_name)), schema_(std::move(schema)) {
  file_.reset(TFile::Open(file_name_.c_str(), "READ"));
  if (!file_ || file_->IsZombie()) {
    throw std::runtime_error("Failed to open ROOT file: " + file_name_);
  }

  tree_ = dynamic_cast<TTree *>(file_->Get(ntuple_name_.c_str()));
  if (!tree_) {
    throw std::runtime_error("Failed to find TTree: " + ntuple_name_);
  }

  reader_ = std::make_unique<TTreeReader>(tree_);
  bind_branches();
}

NanoReader::NanoReader(TTree &tree, BranchSchema schema) : ntuple_name_(tree.GetName()), schema_(std::move(schema)), tree_(&tree) {
  reader_ = std::make_unique<TTreeReader>(tree_);
  bind_branches();
}

void NanoReader::bind_branches() {
  for (const auto &spec : schema_.specs()) {
    if (tree_->GetBranch(spec.name.c_str())) {
      physical_branches_.insert(spec.name);
    }
    if (!tree_->GetBranch(spec.name.c_str())) {
      if (spec.optional) {
        switch (spec.type) {
          case BranchType::kBool:
            fields_[spec.name] = make_scalar_field<bool>();
            break;
          case BranchType::kInt32:
            fields_[spec.name] = make_scalar_field<std::int32_t>();
            break;
          case BranchType::kUInt32:
            fields_[spec.name] = make_scalar_field<std::uint32_t>();
            break;
          case BranchType::kUInt64:
            fields_[spec.name] = make_scalar_field<std::uint64_t>();
            break;
          case BranchType::kFloat:
            fields_[spec.name] = make_scalar_field<float>();
            break;
          case BranchType::kVecBool:
            fields_[spec.name] = make_vector_field<bool>();
            break;
          case BranchType::kVecUInt8:
            fields_[spec.name] = make_vector_field<std::uint8_t>();
            break;
          case BranchType::kVecUInt16:
            fields_[spec.name] = make_vector_field<std::uint16_t>();
            break;
          case BranchType::kVecInt16:
            fields_[spec.name] = make_vector_field<std::int16_t>();
            break;
          case BranchType::kVecInt32:
            fields_[spec.name] = make_vector_field<std::int32_t>();
            break;
          case BranchType::kVecFloat:
            fields_[spec.name] = make_vector_field<float>();
            break;
        }
        continue;
      }
      throw std::runtime_error("Missing branch in TTree: " + spec.name);
    }

    switch (spec.type) {
      case BranchType::kBool:
        fields_[spec.name] = make_scalar_field<bool>();
        loaders_.push_back(std::make_unique<ScalarLoader<Bool_t, bool>>(
            *reader_, spec.name, std::get<std::shared_ptr<bool>>(fields_[spec.name])));
        break;
      case BranchType::kInt32:
        fields_[spec.name] = make_scalar_field<std::int32_t>();
        loaders_.push_back(std::make_unique<ScalarLoader<Int_t, std::int32_t>>(
            *reader_, spec.name, std::get<std::shared_ptr<std::int32_t>>(fields_[spec.name])));
        break;
      case BranchType::kUInt32:
        fields_[spec.name] = make_scalar_field<std::uint32_t>();
        loaders_.push_back(std::make_unique<ScalarLoader<UInt_t, std::uint32_t>>(
            *reader_, spec.name, std::get<std::shared_ptr<std::uint32_t>>(fields_[spec.name])));
        break;
      case BranchType::kUInt64:
        fields_[spec.name] = make_scalar_field<std::uint64_t>();
        loaders_.push_back(std::make_unique<ScalarLoader<ULong64_t, std::uint64_t>>(
            *reader_, spec.name, std::get<std::shared_ptr<std::uint64_t>>(fields_[spec.name])));
        break;
      case BranchType::kFloat:
        fields_[spec.name] = make_scalar_field<float>();
        loaders_.push_back(std::make_unique<ScalarLoader<Float_t, float>>(
            *reader_, spec.name, std::get<std::shared_ptr<float>>(fields_[spec.name])));
        break;
      case BranchType::kVecBool:
        fields_[spec.name] = make_vector_field<bool>();
        loaders_.push_back(std::make_unique<ArrayLoader<Bool_t, bool>>(
            *reader_, spec.name, std::get<std::shared_ptr<std::vector<bool>>>(fields_[spec.name])));
        break;
      case BranchType::kVecUInt8:
        fields_[spec.name] = make_vector_field<std::uint8_t>();
        loaders_.push_back(std::make_unique<ArrayLoader<UChar_t, std::uint8_t>>(
            *reader_, spec.name, std::get<std::shared_ptr<std::vector<std::uint8_t>>>(fields_[spec.name])));
        break;
      case BranchType::kVecUInt16:
        fields_[spec.name] = make_vector_field<std::uint16_t>();
        loaders_.push_back(std::make_unique<ArrayLoader<UShort_t, std::uint16_t>>(
            *reader_, spec.name, std::get<std::shared_ptr<std::vector<std::uint16_t>>>(fields_[spec.name])));
        break;
      case BranchType::kVecInt16:
        fields_[spec.name] = make_vector_field<std::int16_t>();
        loaders_.push_back(std::make_unique<ArrayLoader<Short_t, std::int16_t>>(
            *reader_, spec.name, std::get<std::shared_ptr<std::vector<std::int16_t>>>(fields_[spec.name])));
        break;
      case BranchType::kVecInt32:
        fields_[spec.name] = make_vector_field<std::int32_t>();
        loaders_.push_back(std::make_unique<ArrayLoader<Int_t, std::int32_t>>(
            *reader_, spec.name, std::get<std::shared_ptr<std::vector<std::int32_t>>>(fields_[spec.name])));
        break;
      case BranchType::kVecFloat:
        fields_[spec.name] = make_vector_field<float>();
        loaders_.push_back(std::make_unique<ArrayLoader<Float_t, float>>(
            *reader_, spec.name, std::get<std::shared_ptr<std::vector<float>>>(fields_[spec.name])));
        break;
    }
  }
}

void NanoReader::load(std::size_t entry) {
  reader_->SetEntry(static_cast<Long64_t>(entry));
  for (auto &loader : loaders_) {
    loader->load();
  }
}

std::size_t NanoReader::entries() const {
  return static_cast<std::size_t>(tree_->GetEntries());
}

bool NanoReader::has_physical_branch(std::string_view branch_name) const {
  return physical_branches_.count(std::string(branch_name)) > 0U;
}

const FieldPtr &NanoReader::field(std::string_view branch_name) const {
  auto it = fields_.find(std::string(branch_name));
  if (it == fields_.end()) {
    throw std::out_of_range("Unknown branch: " + std::string(branch_name));
  }
  return it->second;
}

}  // namespace nano
