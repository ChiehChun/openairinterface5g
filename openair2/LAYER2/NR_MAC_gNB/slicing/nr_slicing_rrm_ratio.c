/*
* SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <stdlib.h>
#include "assertions.h"
#include "common/utils/LOG/log.h"
#include "common/utils/nr/nr_common.h"
#include "nr_slicing_rrm_ratio.h"
#include "LAYER2/NR_MAC_gNB/gNB_scheduler_dlsch_default_policies.h"
#include "LAYER2/NR_MAC_gNB/mac_proto.h" // get_dl_slots_per_period()

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

// Per-slice WRR runtime state
typedef struct {
  float credit;
  int slots_won_in_frame; // hard cap enforcement, see nr_dl_rrm_ratio()
} nr_rrm_ratio_state_t;

int nr_dl_rrm_ratio(const nr_dl_sched_params_t *params,
                    nr_slice_config_t *slice_config,
                    nr_dl_candidate_t *candidates,
                    int n_candidates,
                    int bwp_size)
{
  int rb_used = 0;
  /* Handle retransmissions across all slices before any slice winner is decided */
  int n_scheduled = nr_dl_schedule_retx(params, candidates, n_candidates, bwp_size, &rb_used);

  // The credit cap (below) only bounds how much a slice can *bank* while idle, which keeps the
  // long-run win share close to max_ratio but doesn't hard-stop a slice from winning several
  // slots in a row once it cashes that credit in. To actually never exceed max_ratio, track how
  // many slots each slice has already won *this frame* and refuse it another win once it hits
  // its max_ratio's share of this frame's DL slots -- reset at the start of every frame.
  const frame_structure_t *fs = &params->mac->frame_structure;
  const int dl_slots_per_frame = get_dl_slots_per_period(fs) * fs->numb_period_frame;
  const bool new_frame = params->slot == 0;

  /* Weighted Round-Robin (WRR): calculate every slice's credit by its min and max ratios. A
   * slice that hasn't yet reached its min_ratio's worth of slots this frame gets absolute
   * priority over slices only competing for extra above their own guarantee -- min_ratio is
   * thus a hard per-frame floor, symmetric with max_ratio's hard per-frame cap above. Credit
   * only breaks ties within whichever priority tier is competing this slot. */
  bool had_data[NR_MAX_NUM_SLICES] = {0};
  bool eligible[NR_MAX_NUM_SLICES] = {0}; /* has data and hasn't hit its max_ratio cap this frame */
  bool below_min[NR_MAX_NUM_SLICES] = {0}; /* hasn't reached its min_ratio floor yet this frame */
  float credit[NR_MAX_NUM_SLICES];
  for (int s = 0; s < slice_config->num; s++) {
    nr_slice_t *slice = &slice_config->s[s];
    const nr_slice_rrm_ratio_params_t *p = slice->algo_data;
    if (!slice->algo_state) {
      nr_rrm_ratio_state_t *st = calloc(1, sizeof(*st));
      AssertFatal(st, "out of memory\n");
      slice->algo_state = st;
    }
    nr_rrm_ratio_state_t *st = slice->algo_state;
    if (new_frame)
      st->slots_won_in_frame = 0;
    st->credit = min(st->credit + p->min_ratio, (float)p->max_ratio);
    credit[s] = st->credit;

    int min_slots = dl_slots_per_frame * p->min_ratio / 100;
    int max_slots = dl_slots_per_frame * p->max_ratio / 100;
    had_data[s] = nr_dl_slice_pending_bytes(s, candidates, n_candidates) > 0;
    eligible[s] = had_data[s] && st->slots_won_in_frame < max_slots;
    below_min[s] = st->slots_won_in_frame < min_slots;
  }

  /* Pass 1: prefer an eligible slice still below its min_ratio floor. Pass 2: only if none
   * qualified (everyone with data already met their floor, or nobody has data), fall back to
   * normal credit-based competition among all eligible slices. */
  int winner = -1;
  float winner_credit = 0.0f;
  for (int s = 0; s < slice_config->num; s++) {
    if (!eligible[s] || !below_min[s])
      continue;
    if (winner < 0 || credit[s] > winner_credit) {
      winner = s;
      winner_credit = credit[s];
    }
  }
  if (winner < 0) {
    for (int s = 0; s < slice_config->num; s++) {
      if (!eligible[s])
        continue;
      if (winner < 0 || credit[s] > winner_credit) {
        winner = s;
        winner_credit = credit[s];
      }
    }
  }

  if (winner < 0)
    return n_scheduled; /* nothing to send, or every slice with data is already at its max_ratio cap for this frame */

  nr_rrm_ratio_state_t *win_st = slice_config->s[winner].algo_state;
  win_st->credit -= 100.0f; /* a deficit earned back over future slots */
  win_st->slots_won_in_frame++;

  /* Keep only the winning slice's data on each candidate */
  for (int i = 0; i < n_candidates; i++)
    nr_dl_restrict_candidate_to_slice(slice_config, winner, &candidates[i]);

  /* Winner gets the whole slot's remaining RBs */
  const nr_dl_group_t winner_group = {.slice_idx = winner, .candidates = candidates, .count = n_candidates};
  nr_dl_log_group_budget(params, slice_config, &winner_group, bwp_size - rb_used, bwp_size);
  /* Handle new transmission */
  n_scheduled += nr_dl_schedule_newtx_budgeted(params, candidates, n_candidates, bwp_size, &rb_used);
  nr_dl_log_slice_usage(params, slice_config, &winner_group, bwp_size);

  /* log a zero budget for every other slice that had data but lost this round */
  // for (int s = 0; s < slice_config->num; s++) {
  //   if (s == winner || !had_data[s])
  //     continue;
  //   const nr_dl_group_t lost_group = {.slice_idx = s, .candidates = candidates, .count = 0};
  //   nr_dl_log_group_budget(params, slice_config, &lost_group, 0, bwp_size);
  // }

  return n_scheduled;
}
