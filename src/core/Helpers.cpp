#include "nano/core/Helpers.h"

namespace nano {

float delta_phi(float phi1, float phi2) {
  float dphi = phi1 - phi2;
  constexpr float kPi = static_cast<float>(M_PI);
  while (dphi > kPi) {
    dphi -= 2.0f * kPi;
  }
  while (dphi < -kPi) {
    dphi += 2.0f * kPi;
  }
  return dphi;
}

float delta_phi(const ObjectView &a, const ObjectView &b) {
  return delta_phi(a.phi(), b.phi());
}

float delta_r(const ObjectView &a, const ObjectView &b) {
  const auto deta = a.eta() - b.eta();
  const auto dphi = delta_phi(a, b);
  return std::sqrt(deta * deta + dphi * dphi);
}

LorentzVector polar_p4(const ObjectView &obj) {
  return LorentzVector(obj.pt(), obj.eta(), obj.phi(), obj.mass());
}

LorentzVector met_p4(float pt, float phi) {
  return LorentzVector(pt, 0.0f, phi, 0.0f);
}

std::pair<int, float> closest_index(const ObjectView &obj, const std::vector<ObjectView> &collection) {
  int best_index = -1;
  float best_dr = 1000.0f;
  for (std::size_t i = 0; i < collection.size(); ++i) {
    const auto dr = delta_r(obj, collection[i]);
    if (dr < best_dr) {
      best_dr = dr;
      best_index = static_cast<int>(i);
    }
  }
  return {best_index, best_dr};
}

bool safe_bool(const Event &event, std::string_view branch_name) {
  const auto *info = event.schema().find(branch_name);
  if (!info) {
    return false;
  }
  try {
    return event.scalar<bool>(branch_name);
  } catch (...) {
    return false;
  }
}

float safe_object_float(const ObjectView &obj, std::string_view attr, float fallback) {
  try {
    return obj.get<float>(attr);
  } catch (...) {
    return fallback;
  }
}

std::int32_t safe_object_int(const ObjectView &obj, std::string_view attr, std::int32_t fallback) {
  try {
    return obj.get<std::int32_t>(attr);
  } catch (...) {
    return fallback;
  }
}

namespace {

bool pass_v9_jet_id(const ObjectView &jet, bool tight_lep_veto) {
  return (safe_object_int(jet, "jetId", 0) & (tight_lep_veto ? 4 : 2)) != 0;
}

bool pass_v12_jet_id(const ObjectView &jet, bool tight_lep_veto) {
  const auto abs_eta = std::abs(jet.eta());
  bool pass_tight = false;
  if (abs_eta <= 2.7f) {
    pass_tight = (safe_object_int(jet, "jetId", 0) & (1 << 1)) != 0;
  } else if (abs_eta <= 3.0f) {
    pass_tight = ((safe_object_int(jet, "jetId", 0) & (1 << 1)) != 0) && safe_object_float(jet, "neHEF", 1.0f) < 0.99f;
  } else {
    pass_tight = ((safe_object_int(jet, "jetId", 0) & (1 << 1)) != 0) && safe_object_float(jet, "neEmEF", 1.0f) < 0.4f;
  }

  if (!tight_lep_veto || abs_eta > 2.7f) {
    return pass_tight;
  }
  return pass_tight && safe_object_float(jet, "muEF", 0.0f) < 0.8f && safe_object_float(jet, "chEmEF", 0.0f) < 0.8f;
}

bool pass_v15_jet_id(const ObjectView &jet, bool tight_lep_veto) {
  const auto abs_eta = std::abs(jet.eta());
  bool pass_tight = false;
  if (abs_eta <= 2.6f) {
    pass_tight = safe_object_float(jet, "neHEF", 1.0f) < 0.99f && safe_object_float(jet, "neEmEF", 1.0f) < 0.9f &&
                 safe_object_int(jet, "chMultiplicity", 0) + safe_object_int(jet, "neMultiplicity", 0) > 1 &&
                 safe_object_float(jet, "chHEF", 0.0f) > 0.01f && safe_object_int(jet, "chMultiplicity", 0) > 0;
  } else if (abs_eta <= 2.7f) {
    pass_tight = safe_object_float(jet, "neHEF", 1.0f) < 0.9f && safe_object_float(jet, "neEmEF", 1.0f) < 0.99f;
  } else if (abs_eta <= 3.0f) {
    pass_tight = safe_object_float(jet, "neHEF", 1.0f) < 0.99f;
  } else {
    pass_tight = safe_object_int(jet, "neMultiplicity", 0) >= 2 && safe_object_float(jet, "neEmEF", 1.0f) < 0.4f;
  }

  if (!tight_lep_veto || abs_eta > 2.7f) {
    return pass_tight;
  }
  return pass_tight && safe_object_float(jet, "muEF", 0.0f) < 0.8f && safe_object_float(jet, "chEmEF", 0.0f) < 0.8f;
}

}  // namespace

bool pass_jet_id(const ObjectView &jet, std::string_view nano_version, bool tight_lep_veto) {
  if (nano_version == "v9") {
    return pass_v9_jet_id(jet, tight_lep_veto);
  }
  if (nano_version == "v12") {
    return pass_v12_jet_id(jet, tight_lep_veto);
  }
  return pass_v15_jet_id(jet, tight_lep_veto);
}

bool pass_trigger(const Event &event, const std::vector<std::string> &triggers) {
  for (const auto &trigger : triggers) {
    if (safe_bool(event, trigger)) {
      return true;
    }
  }
  return false;
}

}  // namespace nano
