/*
* SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef NR_SLICING_COMMON_H
#define NR_SLICING_COMMON_H

#include "common/platform_types.h"
#include "common/5g_platform_types.h"
#include "common/utils/nr/nr_common.h"

typedef struct nr_slice_s {
  nssai_t nssai;
  char *label;

  void *algo_data;
  void *algo_state;

  uint64_t stat_rbs_budget;
  uint64_t stat_rbs_used;
  uint64_t stat_rbs_avail;
} nr_slice_t;

typedef struct nr_slice_config_s {
  uint8_t num;
  nr_slice_t s[NR_MAX_NUM_SLICES];
} nr_slice_config_t;

int find_slice_idx_by_nssai(const nr_slice_config_t *conf, nssai_t nssai);

int nr_slicing_addmod_slice(nr_slice_config_t *conf, nssai_t nssai, const char *label, void *algo_data);

int nr_slicing_remove_slice(nr_slice_config_t *conf, nssai_t nssai);

void nr_slicing_clear(nr_slice_config_t *conf);

#endif /* NR_SLICING_COMMON_H */