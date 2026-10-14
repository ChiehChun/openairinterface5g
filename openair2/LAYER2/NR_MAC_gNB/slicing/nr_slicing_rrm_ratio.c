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

typedef struct {
  int slots_in_window;
  int slots_won;
  uint32_t last_rbs;

  /* Persistent fractional carryover per tier (Bresenham/DDA-style), fed by owe_slots() at every
   * window reset -- see nr_dl_rrm_ratio(). A slice owed e.g. 1.8 slots/window (dl_slots_per_frame
   * * ratio/100) gets 2 slots most windows and 1 slot occasionally, converging to *exactly* its
   * configured ratio over many windows. This replaces a per-window Largest Remainder Method
   * (floor() + hand the few leftover slots to whichever slice has the biggest fractional
   * remainder that window): with static ratios and a constant dl_slots_per_frame, LRM computes
   * the identical remainder ordering every single window, so the same slice wins that "bonus"
   * slot forever and a smaller-share neighbor loses it forever -- a permanent bias, not just a
   * one-window rounding artifact. Carrying the fraction forward per-slice instead means every
   * slice eventually cashes in its own accumulated fraction, with no cross-slice bias. */
  float ded_owed;
  float min_owed;
  float max_owed;

  int ded_target;
  int min_target;
  int max_target;
} nr_rrm_ratio_state_t;

/* Grants this window's target slot count for one ratio/tier, carrying the fractional remainder
 * forward in `*owed` instead of discarding it -- see nr_rrm_ratio_state_t. */
static int owe_slots(float *owed, int total, int ratio)
{
  *owed += total * ratio / 100.0f;
  int target = (int)*owed; /* floor, since *owed stays >= 0 */
  *owed -= target;
  return target;
}

int nr_dl_rrm_ratio(const nr_dl_sched_params_t *params,
                    nr_slice_config_t *slice_config,
                    nr_dl_candidate_t *candidates,
                    int n_candidates,
                    int bwp_size)
{
  int rb_used = 0;
  /* Handle retransmissions across all slices before any slice winner is decided */
  int n_scheduled = nr_dl_schedule_retx(params, candidates, n_candidates, bwp_size, &rb_used);

  const frame_structure_t *fs = &params->mac->frame_structure;
  const int dl_slots_per_frame = get_dl_slots_per_period(fs) * fs->numb_period_frame;

  for (int s = 0; s < slice_config->num; s++) {
    if (!slice_config->s[s].algo_state) {
      nr_rrm_ratio_state_t *st = calloc(1, sizeof(*st));
      AssertFatal(st, "out of memory\n");
      slice_config->s[s].algo_state = st;
    }
  }

  /* Every slice's algo_state is touched exactly once per call (here, and in the scoring loop
   * below), so their slots_in_window counters stay in lockstep -- checking slice 0 alone is
   * enough to detect a window boundary for all of them. */
  nr_rrm_ratio_state_t *st0 = slice_config->s[0].algo_state;
  bool window_reset = ++st0->slots_in_window > dl_slots_per_frame;
  for (int s = 1; s < slice_config->num; s++)
    ((nr_rrm_ratio_state_t *)slice_config->s[s].algo_state)->slots_in_window = st0->slots_in_window;

  if (window_reset) {
    for (int s = 0; s < slice_config->num; s++) {
      nr_slice_t *slice = &slice_config->s[s];
      nr_rrm_ratio_state_t *st = slice->algo_state;
      // Once per window (not per slot): shows whether last window's apportioned targets were met.
      LOG_D(NR_MAC,
            "[%d.%d] slice nssai=%d.0x%06x (%s): won %d/%d slots last window (ded_target=%d min_target=%d "
            "dl_slots_per_frame=%d) last_rbs=%u\n",
            params->frame,
            params->slot,
            slice->nssai.sst,
            slice->nssai.sd,
            slice->label ? slice->label : "?",
            st->slots_won,
            st->max_target,
            st->ded_target,
            st->min_target,
            dl_slots_per_frame,
            st->last_rbs);
      st->slots_in_window = 1;
      st->slots_won = 0;
      st->last_rbs = 0;

      const nr_slice_rrm_ratio_params_t *p = slice->algo_data;
      st->ded_target = owe_slots(&st->ded_owed, dl_slots_per_frame, p->dedicated_ratio);
      st->min_target = owe_slots(&st->min_owed, dl_slots_per_frame, p->min_ratio);
      st->max_target = owe_slots(&st->max_owed, dl_slots_per_frame, p->max_ratio);
    }
  }

  /* Score-based slice selection (ported from epr_nr_dl()): a slice below its apportioned
   * ded_target gets absolute priority (reserved outright, granted even without current demand --
   * TS 28.541 dedicated_ratio is exclusive, unlike min/max); below min_target it gets priority
   * over slices only competing for extra above their guarantee; below max_target it's only
   * opportunistically eligible. The min/max tiers additionally require live pending-byte demand
   * (nr_dl_slice_pending_bytes()) -- stronger than epr's original last-window-usage check -- and
   * are ranked by how many RBs the slice actually used last time it won. A slice with no eligible
   * tier this call does not win, even if every other slice is capped -- unlike epr_nr_dl(), which
   * could pick a zero-score slice by default (see the "Bug: ... could not find PUCCH for DL DCI"
   * comment in that function). The tie-break among multiple slices below their ded_target this
   * same call is just "biggest absolute remaining count wins" -- unlike ded_target/min_target/
   * max_target themselves, this ordering doesn't need to be fairness-aware, since each slice's
   * own owe_slots() carryover already guarantees its long-run share regardless of who goes
   * first on any particular call. */
  int winner = -1;
  float best_score = 0.0f;
  for (int s = 0; s < slice_config->num; s++) {
    nr_slice_t *slice = &slice_config->s[s];
    nr_rrm_ratio_state_t *st = slice->algo_state;

    if (st->slots_won >= st->max_target)
      continue; /* hard per-window cap reached: not eligible this window anymore */

    bool had_demand = nr_dl_slice_pending_bytes(s, candidates, n_candidates) > 0;
    float score = 0.0f;
    if (st->slots_won < st->ded_target)
      score = 10000.0f + (st->ded_target - st->slots_won);
    else if (had_demand && st->slots_won < st->min_target)
      score = 1000.0f + st->last_rbs + (st->min_target - st->slots_won);
    else if (had_demand && st->slots_won < st->max_target)
      score = 100.0f + st->last_rbs + (st->max_target - st->slots_won);

    if (score > 0.0f && (winner < 0 || score > best_score)) {
      winner = s;
      best_score = score;
    }
  }

  if (winner < 0)
    return n_scheduled; /* nothing to send, or every slice with demand is already at its cap this window */

  nr_rrm_ratio_state_t *win_st = slice_config->s[winner].algo_state;
  win_st->slots_won++;

  /* Keep only the winning slice's data on each candidate */
  for (int i = 0; i < n_candidates; i++)
    nr_dl_restrict_candidate_to_slice(slice_config, winner, &candidates[i]);

  /* Winner gets the whole slot's remaining RBs */
  nr_dl_log_group_budget(params, slice_config, winner, bwp_size - rb_used, bwp_size);
  /* Handle new transmission */
  n_scheduled += nr_dl_schedule_newtx_budgeted(params, candidates, n_candidates, bwp_size, &rb_used);
  win_st->last_rbs = nr_dl_log_slice_usage(params, slice_config, winner, candidates, n_candidates, bwp_size);

  return n_scheduled;
}
