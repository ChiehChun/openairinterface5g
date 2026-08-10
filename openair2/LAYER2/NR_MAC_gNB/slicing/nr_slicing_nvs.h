/*
* SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef NR_SLICING_NVS_H
#define NR_SLICING_NVS_H

#include "nr_slicing_common.h"
#include "LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

typedef struct nr_slice_nvs_params {
  /// target share of RBs won over time, 0-100 (percent)
  int pct_reserved;
} nr_slice_nvs_params_t;

nr_slice_nvs_params_t *nr_slice_nvs_params_new(int pct_reserved);

int nr_dl_nvs(const nr_dl_sched_params_t *params,
              nr_slice_config_t *slice_config,
              nr_dl_candidate_t *candidates,
              int n_candidates,
              int bwp_size);

#endif /* NR_SLICING_NVS_H */
