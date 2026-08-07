/*
* SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_slicing_common.h"
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
