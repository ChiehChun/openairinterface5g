/*
* SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef NR_SLICING_RRM_RATIO_H
#define NR_SLICING_RRM_RATIO_H

#include "nr_slicing_common.h"
#include "LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

// 3GPP TS 28.541 RRMPolicyRatio
typedef struct nr_slice_rrm_ratio_params {
  int dedicated_ratio;
  int min_ratio;
  int max_ratio;
} nr_slice_rrm_ratio_params_t;

nr_slice_rrm_ratio_params_t *nr_slice_rrm_ratio_params_new(int dedicated_ratio, int min_ratio, int max_ratio);

int nr_dl_rrm_ratio(const nr_dl_sched_params_t *params,
                    nr_slice_config_t *slice_config,
                    nr_dl_candidate_t *candidates,
                    int n_candidates,
                    int bwp_size);

#endif /* NR_SLICING_RRM_RATIO_H */
