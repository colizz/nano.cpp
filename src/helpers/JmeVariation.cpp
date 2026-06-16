#include "nano/helpers/JmeVariation.h"

#include <sstream>
#include <stdexcept>

namespace nano {

std::string_view variation_name(JmeVariation variation) {
  switch (variation) {
    case JmeVariation::Nominal:
      return "nominal";
    case JmeVariation::JesUp:
      return "jes_up";
    case JmeVariation::JesDown:
      return "jes_down";
    case JmeVariation::JerUp:
      return "jer_up";
    case JmeVariation::JerDown:
      return "jer_down";
    case JmeVariation::MetUp:
      return "met_up";
    case JmeVariation::MetDown:
      return "met_down";
  }
  return "nominal";
}

JmeVariation parse_jme_variation(std::string_view name) {
  if (name == "nominal") {
    return JmeVariation::Nominal;
  }
  if (name == "jes_up") {
    return JmeVariation::JesUp;
  }
  if (name == "jes_down") {
    return JmeVariation::JesDown;
  }
  if (name == "jer_up") {
    return JmeVariation::JerUp;
  }
  if (name == "jer_down") {
    return JmeVariation::JerDown;
  }
  if (name == "met_up") {
    return JmeVariation::MetUp;
  }
  if (name == "met_down") {
    return JmeVariation::MetDown;
  }
  throw std::runtime_error("Unsupported JME variation: " + std::string(name));
}

std::vector<JmeVariation> parse_jme_variation_list(std::string_view text) {
  if (text == "all") {
    return {JmeVariation::Nominal, JmeVariation::JesUp, JmeVariation::JesDown, JmeVariation::JerUp,
            JmeVariation::JerDown, JmeVariation::MetUp, JmeVariation::MetDown};
  }
  std::vector<JmeVariation> out;
  std::stringstream ss{std::string(text)};
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (!item.empty()) {
      out.push_back(parse_jme_variation(item));
    }
  }
  return out;
}

}  // namespace nano
