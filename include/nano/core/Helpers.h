#pragma once

#include "nano/core/Collection.h"

#include <Math/Vector4D.h>

#include <cmath>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace nano {

using LorentzVector = ROOT::Math::PtEtaPhiMVector;

float delta_phi(float phi1, float phi2);
float delta_phi(const ObjectView &a, const ObjectView &b);
float delta_r(const ObjectView &a, const ObjectView &b);
LorentzVector polar_p4(const ObjectView &obj);
LorentzVector met_p4(float pt, float phi);

std::pair<int, float> closest_index(const ObjectView &obj, const std::vector<ObjectView> &collection);
bool safe_bool(const Event &event, std::string_view branch_name);
float safe_object_float(const ObjectView &obj, std::string_view attr, float fallback = -99.0f);
std::int32_t safe_object_int(const ObjectView &obj, std::string_view attr, std::int32_t fallback = -1);
bool pass_jet_id(const ObjectView &jet, std::string_view nano_version, bool tight_lep_veto);
bool pass_trigger(const Event &event, const std::vector<std::string> &triggers);

}  // namespace nano
