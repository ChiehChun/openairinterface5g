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
             nr_dl_candidate_t *candidates,
             int n_candidates,
             int bwp_size)
{
  int rb_used = 0;
  /* Handle retransmissions across all slices before any slice winner is decided */
  int n_scheduled = nr_dl_schedule_retx(params, candidates, n_candidates, bwp_size, &rb_used);

  /* Pick the slice with the highest NVS weight among those with new-tx data this slot. The
   * reserved default slice (nssai {0,0}) competes here like any other */
  float max_w = 0.0f;
  int winner = -1;
  for (int s = 0; s < slice_config->num; s++) {
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

    bool has_data = nr_dl_slice_pending_bytes(s, candidates, n_candidates) > 0;
    if (has_data && w > max_w) {
      max_w = w;
      winner = s;
    }
  }

  if (winner < 0)
    return n_scheduled; /* nothing to send this slot beyond the retransmissions above */

  nr_slice_nvs_state_t *winner_st = slice_config->s[winner].algo_state;
  winner_st->won_last_round = true;

  /* Keep only the winning slice's data on each candidate */
  for (int i = 0; i < n_candidates; i++)
    nr_dl_restrict_candidate_to_slice(slice_config, winner, &candidates[i]);

  /* Winner gets the whole slot as its budget -- NVS arbitrates in the time domain, not frequency */
  const nr_dl_group_t winner_group = {.slice_idx = winner, .candidates = candidates, .count = n_candidates};
  nr_dl_log_group_budget(params, slice_config, &winner_group, bwp_size - rb_used, bwp_size);
  n_scheduled += nr_dl_schedule_newtx_budgeted(params, candidates, n_candidates, bwp_size, &rb_used);
  nr_dl_log_slice_usage(params, slice_config, &winner_group, bwp_size);

  return n_scheduled;
}
