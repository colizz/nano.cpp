#include "nano/core/Event.h"
#include "nano/core/Collection.h"

namespace nano {

Event::Event(NanoReader &reader, std::size_t entry) : reader_(reader), entry_(entry) {
  reader_.load(entry_);
}

Collection Event::collection(std::string_view name) const {
  // Resolve aliases like "FatJets" -> "FatJet" here so producers can use the
  // more natural collection naming without caring about branch spelling.
  return Collection(const_cast<Event &>(*this), reader_.schema().canonical_object_name(name));
}

bool Event::has(std::string_view name) const {
  return attachments_.count(std::string(name)) > 0U;
}

bool Event::has_physical_branch(std::string_view branch_name) const {
  return reader_.has_physical_branch(branch_name);
}

bool Event::is_mc() const {
  return reader_.has_physical_branch("genWeight");
}

AnyMap &Event::object_extras(std::string_view object_name, std::size_t index) {
  return object_attachments_[std::string(object_name)][index];
}

const AnyMap *Event::find_object_extras(std::string_view object_name, std::size_t index) const {
  auto obj_it = object_attachments_.find(std::string(object_name));
  if (obj_it == object_attachments_.end()) {
    return nullptr;
  }
  auto idx_it = obj_it->second.find(index);
  if (idx_it == obj_it->second.end()) {
    return nullptr;
  }
  return &idx_it->second;
}

}  // namespace nano
