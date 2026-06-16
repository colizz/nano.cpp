#pragma once

#include "nano/io/NanoReader.h"
#include "nano/core/NanoTypes.h"

#include <Math/Vector4D.h>

#include <any>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace nano {

class Collection;

class Event {
public:
  using LorentzVector = ROOT::Math::PtEtaPhiMVector;

  // Constructing an Event loads a single RNTuple entry and provides an
  // attachment space for producer-level derived data.
  Event(NanoReader &reader, std::size_t entry);

  std::size_t entry() const { return entry_; }
  Collection collection(std::string_view name) const;
  const BranchSchema &schema() const { return reader_.schema(); }

  template <typename T>
  T scalar(std::string_view branch_name) const;

  template <typename T>
  const std::vector<T> &vector(std::string_view branch_name) const;

  template <typename T>
  void set(std::string name, T value) {
    attachments_[std::move(name)] = std::move(value);
  }

  template <typename T>
  T &get(std::string_view name) {
    return std::any_cast<T &>(attachments_.at(std::string(name)));
  }

  template <typename T>
  const T &get(std::string_view name) const {
    return std::any_cast<const T &>(attachments_.at(std::string(name)));
  }

  bool has(std::string_view name) const;
  bool has_physical_branch(std::string_view branch_name) const;
  bool is_mc() const;

  // Object extras are keyed by collection name and object index so producers
  // can attach derived quantities such as subjets or a cached p4.
  AnyMap &object_extras(std::string_view object_name, std::size_t index);
  const AnyMap *find_object_extras(std::string_view object_name, std::size_t index) const;

private:
  NanoReader &reader_;
  std::size_t entry_ = 0;
  AnyMap attachments_;
  std::unordered_map<std::string, std::unordered_map<std::size_t, AnyMap>> object_attachments_;
};

template <typename T>
T Event::scalar(std::string_view branch_name) const {
  // Scalar access stays fully typed but avoids exposing ROOT field pointers to
  // producer code.
  const auto &field = reader_.field(branch_name);
  if constexpr (std::is_same_v<T, bool>) {
    return *std::get<std::shared_ptr<bool>>(field);
  } else if constexpr (std::is_same_v<T, std::int32_t>) {
    return *std::get<std::shared_ptr<std::int32_t>>(field);
  } else if constexpr (std::is_same_v<T, std::uint32_t>) {
    return *std::get<std::shared_ptr<std::uint32_t>>(field);
  } else if constexpr (std::is_same_v<T, std::uint64_t>) {
    return *std::get<std::shared_ptr<std::uint64_t>>(field);
  } else if constexpr (std::is_same_v<T, float>) {
    return *std::get<std::shared_ptr<float>>(field);
  }
}

template <typename T>
const std::vector<T> &Event::vector(std::string_view branch_name) const {
  const auto &field = reader_.field(branch_name);
  if constexpr (std::is_same_v<T, bool>) {
    return *std::get<std::shared_ptr<std::vector<bool>>>(field);
  } else if constexpr (std::is_same_v<T, std::uint8_t>) {
    return *std::get<std::shared_ptr<std::vector<std::uint8_t>>>(field);
  } else if constexpr (std::is_same_v<T, std::uint16_t>) {
    return *std::get<std::shared_ptr<std::vector<std::uint16_t>>>(field);
  } else if constexpr (std::is_same_v<T, std::int32_t>) {
    return *std::get<std::shared_ptr<std::vector<std::int32_t>>>(field);
  } else if constexpr (std::is_same_v<T, std::int16_t>) {
    return *std::get<std::shared_ptr<std::vector<std::int16_t>>>(field);
  } else if constexpr (std::is_same_v<T, float>) {
    return *std::get<std::shared_ptr<std::vector<float>>>(field);
  }
}

}  // namespace nano
