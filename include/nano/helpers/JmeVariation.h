#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace nano {

enum class JmeVariation {
  Nominal,
  JesUp,
  JesDown,
  JerUp,
  JerDown,
  MetUp,
  MetDown,
  MuonScaleUp,
  MuonScaleDown,
  MuonSmearUp,
  MuonSmearDown,
};

std::string_view variation_name(JmeVariation variation);
JmeVariation parse_jme_variation(std::string_view name);
std::vector<JmeVariation> parse_jme_variation_list(std::string_view text);
bool is_muon_variation(JmeVariation variation);

}  // namespace nano
