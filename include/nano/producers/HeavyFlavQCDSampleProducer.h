#pragma once

#include "nano/producers/HeavyFlavBaseProducer.h"

namespace nano {

class HeavyFlavQCDSampleProducer : public HeavyFlavBaseProducer {
public:
  explicit HeavyFlavQCDSampleProducer(ProducerConfig config);

  void begin_file() override;
  bool analyze(Event &event) override;
  bool analyze_common(Event &event) override;
  bool analyze_variation(Event &event, const JmeEventResult &jme_result,
                         JmeVariation variation) override;

protected:
  std::size_t output_fatjet_count() const override { return 2U; }

private:
  void select_secondary_vertices(Event &event) const;
  void match_secondary_vertices(
      std::vector<ObjectView> &fatjets,
      const std::vector<ObjectView> &secondary_vertices) const;
  void define_fatjet_sv_branches(std::size_t fatjet_index);
  void fill_fatjet_sv_info(const std::vector<ObjectView> &fatjets,
                           std::size_t fatjet_index);

  bool apply_sv_criteria_ = true;
  bool fill_sv_ = true;
};

} // namespace nano
