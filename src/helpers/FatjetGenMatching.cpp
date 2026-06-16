#include "nano/helpers/FatjetGenMatching.h"

#include "nano/core/Collection.h"
#include "nano/core/Helpers.h"

#include <algorithm>
#include <cmath>

namespace nano {

namespace {

std::int32_t safe_int(const ObjectView &obj, std::string_view attr, std::int32_t fallback = -1) {
  try {
    return obj.get<std::int32_t>(attr);
  } catch (...) {
    return fallback;
  }
}

std::vector<int> &daughter_indices(ObjectView &gp) {
  return gp.extra_ref<std::vector<int>>("dauIdx");
}

const std::vector<int> &daughter_indices(const ObjectView &gp) {
  return gp.extra<std::vector<int>>("dauIdx");
}

ObjectView final_copy(std::vector<ObjectView> &genparts, ObjectView gp) {
  for (const auto idx : daughter_indices(gp)) {
    auto dau = genparts[static_cast<std::size_t>(idx)];
    if (dau.get<std::int32_t>("pdgId") == gp.get<std::int32_t>("pdgId")) {
      return final_copy(genparts, dau);
    }
  }
  return gp;
}

bool is_hadronic(const std::vector<ObjectView> &genparts, const ObjectView &gp) {
  for (const auto idx : daughter_indices(gp)) {
    if (std::abs(genparts[static_cast<std::size_t>(idx)].get<std::int32_t>("pdgId")) < 6) {
      return true;
    }
  }
  return false;
}

float max_delta_r_to_daughters(const ObjectView &jet, const std::vector<ObjectView> &daughters) {
  if (daughters.empty()) {
    return 99.0f;
  }
  float best = 0.0f;
  for (const auto &dau : daughters) {
    best = std::max(best, delta_r(jet, dau));
  }
  return best;
}

}  // namespace

void FatjetGenMatching::process(Event &event, std::vector<ObjectView> &fatjets) const {
  if (!event.is_mc()) {
    return;
  }

  auto genparts = event.collection("GenPart").objects();
  for (auto &gp : genparts) {
    gp.set("dauIdx", std::vector<int>{});
  }
  for (std::size_t idx = 0; idx < genparts.size(); ++idx) {
    const auto mother_idx = safe_int(genparts[idx], "genPartIdxMother", -1);
    if (mother_idx >= 0 && static_cast<std::size_t>(mother_idx) < genparts.size()) {
      daughter_indices(genparts[static_cast<std::size_t>(mother_idx)]).push_back(static_cast<int>(idx));
    }
  }

  std::vector<ObjectView> had_gen_tops;
  std::vector<ObjectView> had_gen_ws;
  std::vector<ObjectView> had_gen_zs;
  std::vector<ObjectView> had_gen_hs;

  for (auto &gp : genparts) {
    if ((gp.get<std::int32_t>("statusFlags") & (1 << 13)) == 0) {
      continue;
    }
    const auto apdg = std::abs(gp.get<std::int32_t>("pdgId"));
    if (apdg == 6) {
      ObjectView final_w;
      ObjectView gen_b;
      bool has_final_w = false;
      bool has_b = false;
      for (const auto dau_idx : daughter_indices(gp)) {
        auto dau = genparts[static_cast<std::size_t>(dau_idx)];
        const auto dau_apdg = std::abs(dau.get<std::int32_t>("pdgId"));
        if (dau_apdg == 24) {
          final_w = final_copy(genparts, dau);
          has_final_w = true;
        } else if (dau_apdg == 1 || dau_apdg == 3 || dau_apdg == 5) {
          gen_b = dau;
          has_b = true;
        }
      }
      if (has_final_w && has_b && is_hadronic(genparts, final_w)) {
        gp.set("genW", final_w);
        gp.set("genB", gen_b);
        had_gen_tops.push_back(gp);
      }
    } else if (apdg == 24 && is_hadronic(genparts, gp)) {
      had_gen_ws.push_back(gp);
    } else if (apdg == 23 && is_hadronic(genparts, gp)) {
      had_gen_zs.push_back(gp);
    } else if (apdg == 25 && is_hadronic(genparts, gp)) {
      had_gen_hs.push_back(gp);
    }
  }

  for (auto &fj : fatjets) {
    const auto [idx_h, dr_h] = closest_index(fj, had_gen_hs);
    const auto [idx_z, dr_z] = closest_index(fj, had_gen_zs);
    const auto [idx_w, dr_w] = closest_index(fj, had_gen_ws);
    const auto [idx_t, dr_t] = closest_index(fj, had_gen_tops);

    fj.set("dr_H", dr_h);
    fj.set("dr_Z", dr_z);
    fj.set("dr_W", dr_w);
    fj.set("dr_T", dr_t);
    fj.set("dr_H_daus", 99.0f);
    fj.set("H_pt", -1.0f);
    fj.set("H_decay", std::int32_t{0});
    fj.set("dr_Z_daus", 99.0f);
    fj.set("Z_pt", -1.0f);
    fj.set("Z_decay", std::int32_t{0});
    fj.set("dr_W_daus", 99.0f);
    fj.set("W_pt", -1.0f);
    fj.set("W_decay", std::int32_t{0});
    fj.set("dr_T_b", 99.0f);
    fj.set("dr_T_Wq_max", 99.0f);
    fj.set("dr_T_Wq_min", 99.0f);
    fj.set("T_Wq_max_pdgId", std::int32_t{0});
    fj.set("T_Wq_min_pdgId", std::int32_t{0});
    fj.set("T_pt", -1.0f);

    if (idx_h >= 0) {
      const auto &gen_h = had_gen_hs[static_cast<std::size_t>(idx_h)];
      std::vector<ObjectView> daughters;
      for (const auto dau_idx : daughter_indices(gen_h)) {
        daughters.push_back(genparts[static_cast<std::size_t>(dau_idx)]);
      }
      fj.set("dr_H_daus", max_delta_r_to_daughters(fj, daughters));
      fj.set("H_pt", gen_h.pt());
      fj.set("H_decay", std::abs(daughters.empty() ? 0 : daughters.front().get<std::int32_t>("pdgId")));
    }
    if (idx_z >= 0) {
      const auto &gen_z = had_gen_zs[static_cast<std::size_t>(idx_z)];
      std::vector<ObjectView> daughters;
      for (const auto dau_idx : daughter_indices(gen_z)) {
        daughters.push_back(genparts[static_cast<std::size_t>(dau_idx)]);
      }
      fj.set("dr_Z_daus", max_delta_r_to_daughters(fj, daughters));
      fj.set("Z_pt", gen_z.pt());
      fj.set("Z_decay", std::abs(daughters.empty() ? 0 : daughters.front().get<std::int32_t>("pdgId")));
    }
    if (idx_w >= 0) {
      const auto &gen_w = had_gen_ws[static_cast<std::size_t>(idx_w)];
      std::vector<ObjectView> daughters;
      for (const auto dau_idx : daughter_indices(gen_w)) {
        daughters.push_back(genparts[static_cast<std::size_t>(dau_idx)]);
      }
      fj.set("dr_W_daus", max_delta_r_to_daughters(fj, daughters));
      fj.set("W_pt", gen_w.pt());
      std::int32_t decay = 0;
      for (const auto &dau : daughters) {
        decay = std::max(decay, std::abs(dau.get<std::int32_t>("pdgId")));
      }
      fj.set("W_decay", decay);
    }
    if (idx_t >= 0) {
      auto gen_t = had_gen_tops[static_cast<std::size_t>(idx_t)];
      const auto gen_b = gen_t.extra<ObjectView>("genB");
      const auto gen_w = gen_t.extra<ObjectView>("genW");
      std::vector<ObjectView> daughters;
      for (const auto dau_idx : daughter_indices(gen_w)) {
        daughters.push_back(genparts[static_cast<std::size_t>(dau_idx)]);
      }
      float dr1 = daughters.size() > 0U ? delta_r(fj, daughters[0]) : 99.0f;
      float dr2 = daughters.size() > 1U ? delta_r(fj, daughters[1]) : 99.0f;
      std::int32_t pdg1 = daughters.size() > 0U ? daughters[0].get<std::int32_t>("pdgId") : 0;
      std::int32_t pdg2 = daughters.size() > 1U ? daughters[1].get<std::int32_t>("pdgId") : 0;
      if (dr1 < dr2) {
        std::swap(dr1, dr2);
        std::swap(pdg1, pdg2);
      }
      fj.set("dr_T_b", delta_r(fj, gen_b));
      fj.set("dr_T_Wq_max", dr1);
      fj.set("dr_T_Wq_min", dr2);
      fj.set("T_Wq_max_pdgId", pdg1);
      fj.set("T_Wq_min_pdgId", pdg2);
      fj.set("T_pt", gen_t.pt());
    }
  }
}

}  // namespace nano
