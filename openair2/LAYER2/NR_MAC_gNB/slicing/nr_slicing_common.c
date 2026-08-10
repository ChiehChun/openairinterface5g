/*
* SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_slicing_common.h"
#include "nr_slicing_rrm_ratio.h"
#include "nr_slicing_nvs.h"
#include "common/utils/nr/nr_common.h"
#include "assertions.h"
#include <string.h>

int find_slice_idx_by_nssai(const nr_slice_config_t *conf, nssai_t nssai)
{
  for (int i = 0; i < conf->num; i++)
    if (conf->s[i].nssai.sst == nssai.sst && conf->s[i].nssai.sd == nssai.sd)
      return i;
  return -1;
}

int nr_slicing_addmod_slice(nr_slice_config_t *conf, nssai_t nssai, const char *label, void *algo_data)
{
  int idx = find_slice_idx_by_nssai(conf, nssai);
  bool is_new = idx < 0;
  if (is_new) {
    if (conf->num >= NR_MAX_NUM_SLICES)
      return -1;
    idx = conf->num++;
  } else {
    free(conf->s[idx].algo_data);
  }

  nr_slice_t *s = &conf->s[idx];
  s->nssai = nssai;
  s->label = label ? strdup(label) : NULL;
  s->algo_data = algo_data;

  if (is_new)
    s->algo_state = NULL;
  return idx;
}

int nr_slicing_remove_slice(nr_slice_config_t *conf, nssai_t nssai)
{
  int idx = find_slice_idx_by_nssai(conf, nssai);
  if (idx < 0)
    return 0;

  free(conf->s[idx].label);
  free(conf->s[idx].algo_data);
  free(conf->s[idx].algo_state);
  const int last = --conf->num;
  if (idx != last)
    conf->s[idx] = conf->s[last];
  return 1;
}

void nr_slicing_clear(nr_slice_config_t *conf)
{
  while (conf->num > 0)
    nr_slicing_remove_slice(conf, conf->s[0].nssai);
}

static bool nssai_is_default(nssai_t n)
{
  return n.sst == 0 && n.sd == 0;
}

bool nr_slice_lc_matches(const nr_slice_config_t *slice_config, int slice_idx, nssai_t lc_nssai)
{
  nssai_t slice_nssai = slice_config->s[slice_idx].nssai;
  if (lc_nssai.sst == slice_nssai.sst && lc_nssai.sd == slice_nssai.sd)
    return true;
  return nssai_is_default(slice_nssai) && find_slice_idx_by_nssai(slice_config, lc_nssai) < 0;
}

void nr_slicing_ensure_default_slice_rrm(nr_slice_config_t *conf)
{
  int sum_others_min = 0;
  for (int i = 0; i < conf->num; i++) {
    if (nssai_is_default(conf->s[i].nssai))
      continue;
    const nr_slice_rrm_ratio_params_t *p = conf->s[i].algo_data;
    if (p)
      sum_others_min += p->min_ratio;
  }
  int ratio = min(max(100 - sum_others_min, NR_SLICE_DEFAULT_MIN_RATIO_FLOOR), 100);

  const nssai_t default_nssai = {0, 0};
  int idx = find_slice_idx_by_nssai(conf, default_nssai);
  if (idx < 0) {
    nr_slice_rrm_ratio_params_t *p = nr_slice_rrm_ratio_params_new(ratio, ratio, 100);
    idx = nr_slicing_addmod_slice(conf, default_nssai, "default", p);
    AssertFatal(idx >= 0, "could not create the reserved default slice\n");
  } else {
    nr_slice_rrm_ratio_params_t *p = conf->s[idx].algo_data;
    p->dedicated_ratio = ratio;
    p->min_ratio = ratio;
    p->max_ratio = 100;
  }
}

void nr_slicing_ensure_default_slice_nvs(nr_slice_config_t *conf)
{
  int sum_others = 0;
  for (int i = 0; i < conf->num; i++) {
    if (nssai_is_default(conf->s[i].nssai))
      continue;
    const nr_slice_nvs_params_t *p = conf->s[i].algo_data;
    if (p)
      sum_others += p->pct_reserved;
  }
  int pct = min(max(100 - sum_others, NR_SLICE_DEFAULT_MIN_RATIO_FLOOR), 100);

  const nssai_t default_nssai = {0, 0};
  int idx = find_slice_idx_by_nssai(conf, default_nssai);
  if (idx < 0) {
    nr_slice_nvs_params_t *p = nr_slice_nvs_params_new(pct);
    idx = nr_slicing_addmod_slice(conf, default_nssai, "default", p);
    AssertFatal(idx >= 0, "could not create the reserved default slice\n");
  } else {
    nr_slice_nvs_params_t *p = conf->s[idx].algo_data;
    p->pct_reserved = pct;
  }
}
