/*
* SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdlib.h>
#include "assertions.h"
#include "common/utils/nr/nr_common.h"
#include "nr_slicing_nvs.h"
#include "LAYER2/NR_MAC_gNB/gNB_scheduler_dlsch_default_policies.h"

#define NR_NVS_BETA 0.001f


typedef struct {
  float exp;
  bool won_last_round;
} nr_slice_nvs_state_t;

nr_slice_nvs_params_t *nr_slice_nvs_params_new(int pct_reserved)
{
  nr_slice_nvs_params_t *p = malloc(sizeof(*p));
  AssertFatal(p, "out of memory\n");
  p->pct_reserved = max(0, min(pct_reserved, 100));
  return p;
}

int nr_dl_nvs(const nr_dl_sched_params_t *params,
             nr_slice_config_t *slice_config,
             nr_dl_group_t groups[NR_MAX_NUM_SLICES + 1],
             int n_groups,
             int bwp_size)
{
  float max_w = 0.0f;
  int max_g = -1;
  for (int g = 0; g < n_groups; g++) {
    int s = groups[g].slice_idx;
    if (s == slice_config->num)
      continue; /* default/unmatched bucket doesn't compete for the NVS weight */

    nr_slice_t *slice = &slice_config->s[s];
    const nr_slice_nvs_params_t *p = slice->algo_data;
    nr_slice_nvs_state_t *st = slice->algo_state;
    if (!st) {
      st = calloc(1, sizeof(*st));
      AssertFatal(st, "out of memory\n");
      st->exp = p->pct_reserved / 100.0f;
      slice->algo_state = st;
    }

    if (st->won_last_round)
      st->exp += NR_NVS_BETA;

    /* weight: reserved share divided by how much of that share this slice
     * has recently gotten (exp) -- a slice starved relative to its
     * reservation has a low exp and thus a high weight, so it wins sooner */
    float w = p->pct_reserved / 100.0f / st->exp;

    st->exp = (1.0f - NR_NVS_BETA) * st->exp;
    st->won_last_round = false; /* overwritten below for the actual winner */

    if (w > max_w) {
      max_w = w;
      max_g = g;
    }
  }

  if (max_g >= 0) {
    nr_slice_nvs_state_t *winner_st = slice_config->s[groups[max_g].slice_idx].algo_state;
    winner_st->won_last_round = true;
  }

  /* Exactly one group runs this slot, with the whole BWP as its budget --
   * NVS arbitrates in the time domain, not the frequency domain: the NVS
   * winner if one competed, otherwise the default/unmatched bucket (if
   * present), same as any other uncapped fallback. Everyone else gets
   * nothing (and isn't even worth logging a zero-budget decision for). */
  int n_scheduled = 0;
  for (int g = 0; g < n_groups; g++) {
    bool this_group_runs = g == max_g || (max_g < 0 && groups[g].slice_idx == slice_config->num);
    if (!this_group_runs)
      continue;
    nr_dl_log_group_budget(params, slice_config, &groups[g], bwp_size, bwp_size);
    n_scheduled += nr_dl_proportional_fair_budgeted(params, groups[g].candidates, groups[g].count, bwp_size);
    nr_dl_log_slice_usage(params, slice_config, &groups[g], bwp_size);
  }
  return n_scheduled;
}
