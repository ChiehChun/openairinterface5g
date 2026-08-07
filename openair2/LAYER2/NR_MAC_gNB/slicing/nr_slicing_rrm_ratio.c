/*
* SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdlib.h>
#include "assertions.h"
#include "common/utils/LOG/log.h"
#include "common/utils/nr/nr_common.h"
#include "nr_slicing_rrm_ratio.h"
#include "LAYER2/NR_MAC_gNB/gNB_scheduler_dlsch_default_policies.h"

nr_slice_rrm_ratio_params_t *nr_slice_rrm_ratio_params_new(int dedicated_ratio, int min_ratio, int max_ratio)
{
  /* enforce 0 <= dedicated_ratio <= min_ratio <= max_ratio <= 100, matching
   * the 3GPP TS 28.541 RRMPolicyRatio ordering (dedicated is a subset of the
   * guaranteed minimum, which in turn is a floor under the maximum) */
  int ded = dedicated_ratio, mn = min_ratio, mx = max_ratio;
  if (ded < 0 || mn < ded || mx < mn || mx > 100) {
    LOG_W(NR_MAC,
          "%s(): inconsistent RRMPolicyRatio (dedicated=%d, min=%d, max=%d), clamping\n",
          __func__,
          dedicated_ratio,
          min_ratio,
          max_ratio);
    ded = max(0, min(ded, 100));
    mn = max(ded, min(mn, 100));
    mx = max(mn, min(mx, 100));
  }

  nr_slice_rrm_ratio_params_t *p = malloc(sizeof(*p));
  AssertFatal(p, "out of memory\n");
  p->dedicated_ratio = ded;
  p->min_ratio = mn;
  p->max_ratio = mx;
  return p;
}

int nr_dl_rrm_ratio(const nr_dl_sched_params_t *params,
                    nr_slice_config_t *slice_config,
                    nr_dl_group_t groups[NR_MAX_NUM_SLICES + 1],
                    int n_groups,
                    int bwp_size)
{
  /* reserve every active real slice's guaranteed floor out of the shared
   * pool up front, so "shared" below only ever hands out genuinely
   * uncommitted RBs */
  int shared_pool = bwp_size;
  for (int g = 0; g < n_groups; g++) {
    if (groups[g].slice_idx == slice_config->num)
      continue;
    const nr_slice_rrm_ratio_params_t *p = slice_config->s[groups[g].slice_idx].algo_data;
    shared_pool -= bwp_size * p->min_ratio / 100;
  }
  shared_pool = max(shared_pool, 0); /* defensive: min ratios summing above 100% */

  int n_scheduled = 0;
  int remain_groups = n_groups;
  for (int g = 0; g < n_groups; g++) {
    const nr_dl_group_t *group = &groups[g];
    bool is_real_slice = group->slice_idx != slice_config->num;

    int rb_budget;
    int min_rbs = 0, ded_rbs = 0;
    if (is_real_slice) {
      const nr_slice_rrm_ratio_params_t *p = slice_config->s[group->slice_idx].algo_data;
      min_rbs = bwp_size * p->min_ratio / 100;
      ded_rbs = bwp_size * p->dedicated_ratio / 100;
      int max_rbs = bwp_size * p->max_ratio / 100;
      int share = shared_pool / remain_groups;
      rb_budget = min(min_rbs + share, max_rbs);
    } else {
      /* default/unmatched bucket: no ratio-based entitlement, absorbs
       * whatever's left in the pool when its turn comes */
      rb_budget = shared_pool;
    }

    nr_dl_log_group_budget(params, slice_config, group, rb_budget, bwp_size);
    n_scheduled += nr_dl_proportional_fair_budgeted(params, group->candidates, group->count, rb_budget);
    int used_rbs = nr_dl_log_slice_usage(params, slice_config, group, bwp_size);

    if (is_real_slice) {
      if (used_rbs > min_rbs)
        shared_pool -= used_rbs - min_rbs; /* dug into the shared pool beyond its own guarantee */
      else if (used_rbs > ded_rbs)
        shared_pool += min_rbs - used_rbs; /* gives back the unused part above its dedicated floor */
      /* else: used_rbs <= ded_rbs -- nothing returned; even the unused
       * non-dedicated part of its guarantee stays reserved for this slice */
    } else {
      shared_pool -= used_rbs;
    }
    remain_groups--;
  }

  return n_scheduled;
}
