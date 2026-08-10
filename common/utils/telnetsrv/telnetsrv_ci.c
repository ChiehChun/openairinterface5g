/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*! \file telnetsrv_ci.c
 * \brief Implementation of telnet CI functions for gNB
 * \note  This file contains telnet-related functions specific to 5G gNB.
 */

#include <sys/types.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "openair2/RRC/NR/rrc_gNB_UE_context.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_ue_manager.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_entity_am.h"
#include "openair2/LAYER2/NR_MAC_gNB/mac_proto.h"
#include "openair2/LAYER2/NR_MAC_gNB/mac_config.h"
#include "openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_dlsch_default_policies.h"
#include "openair2/LAYER2/NR_MAC_gNB/slicing/nr_slicing_rrm_ratio.h"
#include "openair2/LAYER2/NR_MAC_gNB/slicing/nr_slicing_nvs.h"
#include "openair2/RRC/NR/rrc_gNB_mobility.h"
#include "openair3/NGAP/ngap_gNB_ue_context.h"
#include "openair2/RRC/NR/rrc_gNB_du.h"
#define TELNETSERVERCODE
#include "telnetsrv.h"

#define ERROR_MSG_RET(mSG, aRGS...) do { prnt(mSG, ##aRGS); return -1; } while (0)

static int get_single_ue_rnti_mac(void)
{
  NR_UE_info_t *ue = NULL;
  UE_iterator(RC.nrmac[0]->UE_info.connected_ue_list, it) {
    if (it && ue)
      return -1;
    if (it)
      ue = it;
  }
  if (!ue)
    return -1;

  return ue->rnti;
}

int get_single_rnti(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (buf)
    ERROR_MSG_RET("no parameter allowed\n");

  int rnti = get_single_ue_rnti_mac();
  if (rnti < 1)
    ERROR_MSG_RET("different number of UEs\n");

  prnt("single UE RNTI %04x\n", rnti);
  return 0;
}

rrc_gNB_ue_context_t *get_single_rrc_ue(void)
{
  rrc_gNB_ue_context_t *ue = NULL;
  rrc_gNB_ue_context_t *l = NULL;
  int n = 0;
  RB_FOREACH (l, rrc_nr_ue_tree_s, &RC.nrrrc[0]->rrc_ue_head) {
    if (ue == NULL)
      ue = l;
    n++;
  }
  if (!ue) {
    printf("could not find any UE in RRC\n");
  }
  if (n > 1) {
    printf("more than one UE in RRC present\n");
    ue = NULL;
  }

  return ue;
}

int get_reestab_count(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (!RC.nrrrc)
    ERROR_MSG_RET("no RRC present, cannot list counts\n");
  rrc_gNB_ue_context_t *ue = NULL;
  if (!buf) {
    ue = get_single_rrc_ue();
    if (!ue)
      ERROR_MSG_RET("no single UE in RRC present\n");
  } else {
    ue_id_t ue_id = strtol(buf, NULL, 10);
    ue = rrc_gNB_get_ue_context(RC.nrrrc[0], ue_id);
    if (!ue)
      ERROR_MSG_RET("could not find UE with ue_id %d in RRC\n");
  }

  prnt("UE RNTI %04x reestab %d reconfig %d\n",
       ue->ue_context.rnti,
       ue->ue_context.ue_reestablishment_counter,
       ue->ue_context.ue_reconfiguration_counter);
  return 0;
}

int fetch_rnti(char *buf, telnet_printfunc_t prnt)
{
  int rnti = -1;
  if (!buf) {
    rnti = get_single_ue_rnti_mac();
    if (rnti < 1)
      ERROR_MSG_RET("no UE found\n");
  } else {
    rnti = strtol(buf, NULL, 16);
    if (rnti < 1 || rnti >= 0xfffe)
      ERROR_MSG_RET("RNTI needs to be [1,0xfffe]\n");
  }
  return rnti;
}

int trigger_reestab(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (!RC.nrmac)
    ERROR_MSG_RET("no MAC/RLC present, cannot trigger reestablishment\n");
  int rnti = fetch_rnti(buf, prnt);
  if (rnti < 0)
    ERROR_MSG_RET("could not identify UE (no UE, no such RNTI, or multiple UEs)\n");
  nr_rlc_test_trigger_reestablishment(rnti);
  prnt("Reset RLC counters of UE RNTI %04x to trigger reestablishment\n", rnti);
  return 0;
}

/** @brief Get connected DU by the UE ID */
int fetch_du_by_ue_id(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (!RC.nrrrc)
    ERROR_MSG_RET("no RRC present, cannot list counts\n");

  ue_id_t ue_id;
  if (buf) {
    ue_id = strtol(buf, NULL, 10);
  } else {
    // No UE ID provided: find the connected UE first
    rrc_gNB_ue_context_t *ue = get_single_rrc_ue();
    if (!ue)
      ERROR_MSG_RET("no single UE in RRC present\n");
    ue_id = ue->ue_context.rrc_ue_id;
  }

  nr_rrc_du_container_t *du = get_du_for_ue(RC.nrrrc[0], ue_id);

  if (du) {
    prnt("gNB_DU_id %ld is connected to ue_id %ld\n", du->gNB_DU_id, ue_id);
    return 0;
  } else {
    ERROR_MSG_RET("No DU connected\n");
    return -1;
  }
}

/**
 * @brief Trigger F1 handover for UE
 * @param buf: RRC UE ID or NULL for the first UE in list
 * @param debug: Debug flag
 * @param prnt: Print function
 * @return 0 on success, -1 on failure
 */
int rrc_gNB_trigger_f1_ho(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (!RC.nrrrc)
    ERROR_MSG_RET("no RRC present, cannot list counts\n");
  rrc_gNB_ue_context_t *ue = NULL;
  if (!buf) {
    ue = get_single_rrc_ue();
    if (!ue)
      ERROR_MSG_RET("no single UE in RRC present\n");
  } else {
    ue_id_t ue_id = strtol(buf, NULL, 10);
    ue = rrc_gNB_get_ue_context(RC.nrrrc[0], ue_id);
    if (!ue)
      ERROR_MSG_RET("could not find UE with ue_id %d in RRC\n", ue_id);
  }

  gNB_RRC_UE_t *UE = &ue->ue_context;
  nr_HO_F1_trigger_telnet(RC.nrrrc[0], UE->rrc_ue_id);
  prnt("RRC F1 handover triggered for UE %u\n", UE->rrc_ue_id);
  return 0;
}

/** @brief Trigger N2 handover for UE
 *  @param buf: Neighbour PCI, SCell PCI, RRC UE ID
 *  @param debug: Debug flag
 *  @param prnt: Print function
 *  @return 0 on success, -1 on failure */
int rrc_gNB_trigger_n2_ho(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (!RC.nrrrc)
    ERROR_MSG_RET("no RRC present, cannot list counts\n");

  if (!buf) {
    ERROR_MSG_RET("Please provide neighbour cell id and ue id\n");
  } else {
    // Parse neighbour cell PCI
    char *token = strtok(buf, ",");
    if (!token) {
      ERROR_MSG_RET("Invalid input. Expected format: Neighbour PCI, ueId\n");
    }
    uint32_t neighbour_pci = strtol(token, NULL, 10);

    // Parse ueId
    token = strtok(NULL, ",");
    if (!token) {
      ERROR_MSG_RET("Missing UE ID\n");
    }
    uint32_t ueId = strtol(token, NULL, 10);

    // Retrieve UE context
    rrc_gNB_ue_context_t *ue_p = rrc_gNB_get_ue_context(RC.nrrrc[0], ueId);
    if (!ue_p) {
      ERROR_MSG_RET("UE with id %u not found\n", ueId);
    }
    gNB_RRC_UE_t *UE = &ue_p->ue_context;

    // Trigger N2 handover
    nr_HO_N2_trigger_telnet(RC.nrrrc[0], neighbour_pci, UE->rrc_ue_id);

    // Print success message
    prnt("RRC N2 handover triggered for UE %u with neighbour cell id %u\n",
         ueId,
         neighbour_pci);
  }
  return 0;
}

int force_ul_failure(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (!RC.nrmac)
    ERROR_MSG_RET("no MAC/RLC present, force_ul_failure failed\n");
  int rnti = fetch_rnti(buf, prnt);
  if (rnti < 0)
    ERROR_MSG_RET("could not identify UE (no UE, no such RNTI, or multiple UEs)\n");
  NR_UE_info_t *UE = find_nr_UE(&RC.nrmac[0]->UE_info, rnti);
  nr_mac_trigger_ul_failure(&UE->UE_sched_ctrl, UE->current_UL_BWP.scs);
  return 0;
}

int force_ue_release(char *buf, int debug, telnet_printfunc_t prnt)
{
  force_ul_failure(buf, debug, prnt);
  int rnti = fetch_rnti(buf, prnt);
  if (rnti < 0)
    ERROR_MSG_RET("could not identify UE (no UE, no such RNTI, or multiple UEs)\n");
  NR_UE_info_t *UE = find_nr_UE(&RC.nrmac[0]->UE_info, rnti);
  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  sched_ctrl->ul_failure_timer = 2;
  nr_mac_check_ul_failure(RC.nrmac[0], UE->rnti, sched_ctrl);
  return 0;
}

static int get_current_bwp(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  int rnti = fetch_rnti(buf, prnt);
  if (rnti < 0)
    ERROR_MSG_RET("could not identify UE (no UE, no such RNTI, or multiple UEs)\n");
  NR_UE_info_t *UE = find_nr_UE(&RC.nrmac[0]->UE_info, rnti);
  if (!UE)
    ERROR_MSG_RET("could not find UE with RNTI %04x\n", rnti);
  int dl_bwp = UE->current_DL_BWP.bwp_id;
  const char *dl_bwp_text = dl_bwp > 0 ? "dedicated" : "initial";
  int ul_bwp = UE->current_UL_BWP.bwp_id;
  const char *ul_bwp_text = ul_bwp > 0 ? "dedicated" : "initial";

  prnt("UE %04x DL BWP ID %d (%s) UL BWP ID %d (%s)\n", UE->rnti, dl_bwp, dl_bwp_text, ul_bwp, ul_bwp_text);
  return 0;
}

/** @brief Trigger NGAP PDU Session Release for one or more PDU sessions associated with a UE ID/
 *  Syntax: trigger_pdu_session_release [ue_id=gNB_ue_ngap_id(int,opt)],pdusession_id(int)[,pdusession_id(int)...]
 *  - If the gNB_ue_ngap_id is omitted, it is fetched from the only UE present in the RRC layer
 *  - At least one valid PDU session ID must be provided
 * @param[in] buf   Comma-separated input string: [ue_id=gNB_ue_ngap_id(int,opt)],PDU1[,PDU2,...]
 * @param[in] debug Not used.
 * @param[in] prnt  Callback for telnet output printing.
 * @return 0 on success; negative value on error. */
static int trigger_ngap_pdu_session_release(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (buf == NULL) {
    ERROR_MSG_RET("Missing input. Usage: trigger_pdu_session_release [ue_id=gNB_ue_ngap_id(int,opt)],pdusession_id(int)[,pdusession_id(int)...]\n");
  }

  char *tokens[NR_MAX_NB_PDU_SESSIONS + 1];
  int count = 0;

  for (char *tok = strtok(buf, ","); tok != NULL && count < (int)sizeofArray(tokens); tok = strtok(NULL, ",")) {
    tokens[count++] = tok;
  }

  if (count < 1) {
    ERROR_MSG_RET("Invalid input. Usage: trigger_pdu_session_release [ue_id=gNB_ue_ngap_id(int,opt)],pdusession_id(int)[,pdusession_id(int)...]\n");
  }

  int gNB_ue_ngap_id = -1;
  int pdu_start_index = 0;

  if (strncmp(tokens[0], "ue_id=", 6) == 0) {
    gNB_ue_ngap_id = atoi(tokens[0] + 6);
    pdu_start_index = 1;
  } else {
    // No UE ID: infer it
    if (!RC.nrrrc)
      ERROR_MSG_RET("No RRC present\n");
    rrc_gNB_ue_context_t *ue = get_single_rrc_ue();
    if (!ue)
      ERROR_MSG_RET("No single UE in RRC present\n");
    gNB_ue_ngap_id = ue->ue_context.rrc_ue_id;
  }

  if (pdu_start_index >= count) {
    ERROR_MSG_RET("No pdusession_id(int) provided\n");
  }

  ngap_gNB_ue_context_t *ngap = ngap_get_ue_context(gNB_ue_ngap_id);
  if (!ngap) {
    ERROR_MSG_RET("No NGAP UE context for gNB_ue_ngap_id %d\n", gNB_ue_ngap_id);
  }

  MessageDef *message_p = itti_alloc_new_message(TASK_NGAP, 0, NGAP_PDUSESSION_RELEASE_COMMAND);
  ngap_pdusession_release_command_t *msg = &NGAP_PDUSESSION_RELEASE_COMMAND(message_p);
  memset(msg, 0, sizeof(*msg));

  msg->amf_ue_ngap_id = ngap->amf_ue_ngap_id;
  msg->gNB_ue_ngap_id = ngap->gNB_ue_ngap_id;

  int nb_sessions = 0;
  for (int i = pdu_start_index; i < count; ++i) {
    int sid = atoi(tokens[i]);
    if (sid < 1 || sid > 255) {
      ERROR_MSG_RET("Invalid pdusession_id(int): %s (must be between 1 and 255)\n", tokens[i]);
    }
    msg->pdusession_ids[nb_sessions++] = sid;
  }

  msg->nb_pdusessions_torelease = nb_sessions;

  if (prnt) {
    prnt("Triggering NGAP PDU Session Release for gNB_ue_ngap_id=%d: releasing pdusession_id=%d", gNB_ue_ngap_id);
    for (int i = 0; i < nb_sessions; ++i) {
      prnt(" %d,", msg->pdusession_ids[i]);
    }
    prnt("\n");
  }

  itti_send_msg_to_task(TASK_RRC_GNB, 0, message_p);
  return 0;
}

static int trigger_bwp_switch(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  char *sbwpId = strtok(buf, " ");
  int bwpId = atoi(sbwpId);
  char *srnti = strtok(NULL, " ");
  prnt("bwpId %d rnti %s\n", bwpId, srnti);
  int rnti = fetch_rnti(srnti, prnt);
  if (rnti < 0)
    ERROR_MSG_RET("could not identify UE (no UE, no such RNTI, or multiple UEs)\n");
  if (!nr_trigger_bwp_switch(rnti, bwpId)) {
    prnt("failed trigger BWP switch for UE %04x BWP ID %d\n", rnti, bwpId);
    return -1;
  } else {
    prnt("triggered BWP switch to BWP ID %d for UE %04x\n", bwpId, rnti);
    return 0;
  }
}

static int set_pusch_target_snr(char *buf, int debug, telnet_printfunc_t prnt)
{
  if (!buf)
    ERROR_MSG_RET("need an SNR to read\n");

  char *end;
  long new_snr = strtol(buf, &end, 0);
  if (*end != 0)
    ERROR_MSG_RET("error: could not parse number in '%s'\n", buf);

  gNB_MAC_INST *nrmac = RC.nrmac[0];
  NR_SCHED_LOCK(&nrmac->sched_lock);
  UE_iterator(nrmac->UE_info.connected_ue_list, it) {
    nr_mac_set_target_snrx10(&it->UE_sched_ctrl.pusch_pc, new_snr * 10);
  }
  NR_SCHED_UNLOCK(&nrmac->sched_lock);
  prnt("set new PUSCH target SNR %d for all UEs\n", new_snr);

  return 0;
}

// Parses "<sd>" as decimal or 0x-prefixed hex, 0-0xFFFFFF; writes *sd and returns true on success.
static bool parse_sd(const char *str, uint32_t *sd)
{
  char *end;
  long val = strtol(str, &end, 0);
  if (*end != 0 || val < 0 || val > 0xFFFFFF)
    return false;
  *sd = (uint32_t)val;
  return true;
}

// Adds/updates a slice ("slice_add rrm|nvs <sst> <sd> <label> <params...>"); switching algo families clears all existing slices first.
static int slice_add(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (!buf)
    ERROR_MSG_RET("usage: slice_add <rrm|nvs> <sst> <sd> <label> <params...>\n"
                  "  rrm: slice_add rrm <sst> <sd> <label> <dedicated> <min> <max>\n"
                  "  nvs: slice_add nvs <sst> <sd> <label> <pct_reserved>\n");

  char algo_str[8];
  int sst;
  char sd_str[32];
  char label[64];
  int consumed = 0;
  int n = sscanf(buf, "%7s %d %31s %63s%n", algo_str, &sst, sd_str, label, &consumed);
  if (n != 4)
    ERROR_MSG_RET("usage: slice_add <rrm|nvs> <sst> <sd> <label> <params...>\n");

  uint32_t sd;
  if (!parse_sd(sd_str, &sd))
    ERROR_MSG_RET("invalid sd '%s' (decimal or 0x-prefixed hex, 0-0xFFFFFF)\n", sd_str);

  nr_dl_slice_algo_fn target_algo;
  void *algo_data;
  int dedicated = 0, min_ratio = 0, max_ratio = 0, pct_reserved = 0;
  const char *rest = buf + consumed;
  if (strcmp(algo_str, "rrm") == 0) {
    if (sscanf(rest, "%d %d %d", &dedicated, &min_ratio, &max_ratio) != 3)
      ERROR_MSG_RET("usage: slice_add rrm <sst> <sd> <label> <dedicated> <min> <max>\n");
    target_algo = nr_dl_rrm_ratio;
    algo_data = nr_slice_rrm_ratio_params_new(dedicated, min_ratio, max_ratio);
  } else if (strcmp(algo_str, "nvs") == 0) {
    if (sscanf(rest, "%d", &pct_reserved) != 1)
      ERROR_MSG_RET("usage: slice_add nvs <sst> <sd> <label> <pct_reserved>\n");
    target_algo = nr_dl_nvs;
    algo_data = nr_slice_nvs_params_new(pct_reserved);
  } else {
    ERROR_MSG_RET("unknown algo '%s' (expected 'rrm' or 'nvs')\n", algo_str);
  }

  gNB_MAC_INST *mac = RC.nrmac[0];
  AssertFatal(mac != NULL, "need MAC\n");

  nssai_t nssai = {.sst = sst, .sd = sd};

  NR_SCHED_LOCK(&mac->sched_lock);
  if (mac->dl_slice_algo && mac->dl_slice_algo != target_algo && mac->slice_config.num > 0) {
    prnt("WARN: switching slicing algorithm from %s to %s -- clearing %d existing slice(s)\n",
         mac->dl_slice_algo == nr_dl_rrm_ratio ? "rrm" : mac->dl_slice_algo == nr_dl_nvs ? "nvs" : "custom",
         algo_str,
         mac->slice_config.num);
    nr_slicing_clear(&mac->slice_config);
  }

  if (target_algo == nr_dl_rrm_ratio) {
    // min_ratio is a hard reservation out of the shared RB pool (see nr_dl_rrm_ratio()), so the
    // total across all rrm slices must not exceed 100% or slices would silently starve each other.
    int existing_idx = find_slice_idx_by_nssai(&mac->slice_config, nssai);
    int total_min = min_ratio;
    for (int i = 0; i < mac->slice_config.num; i++) {
      if (i == existing_idx)
        continue;
      const nr_slice_rrm_ratio_params_t *p = mac->slice_config.s[i].algo_data;
      total_min += p->min_ratio;
    }
    if (total_min > 100) {
      NR_SCHED_UNLOCK(&mac->sched_lock);
      free(algo_data);
      ERROR_MSG_RET("rejected: total min ratio across all rrm slices would be %d%% (>100%%)\n", total_min);
    }
  }

  int idx = nr_slicing_addmod_slice(&mac->slice_config, nssai, label, algo_data);
  if (idx < 0) {
    NR_SCHED_UNLOCK(&mac->sched_lock);
    free(algo_data);
    ERROR_MSG_RET("could not add slice: config already has the max %d slices\n", NR_MAX_NUM_SLICES);
  }
  mac->dl_slice_algo = target_algo;
  if (mac->dl_rb_alloc != nr_dl_two_level_scheduler)
    mac->dl_rb_alloc = nr_dl_two_level_scheduler;
  NR_SCHED_UNLOCK(&mac->sched_lock);

  if (target_algo == nr_dl_rrm_ratio)
    prnt("OK: slice[%d] algo=rrm nssai=%d.0x%06x label=%s dedicated=%d min=%d max=%d\n",
         idx,
         sst,
         sd,
         label,
         dedicated,
         min_ratio,
         max_ratio);
  else
    prnt("OK: slice[%d] algo=nvs nssai=%d.0x%06x label=%s pct_reserved=%d\n", idx, sst, sd, label, pct_reserved);
  return 0;
}

// Removes a slice matched by exact sst+sd ("slice_remove <sst> <sd>").
static int slice_remove(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (!buf)
    ERROR_MSG_RET("usage: slice_remove <sst> <sd>\n");

  int sst;
  char sd_str[32];
  int n = sscanf(buf, "%d %31s", &sst, sd_str);
  if (n != 2)
    ERROR_MSG_RET("usage: slice_remove <sst> <sd>\n");

  uint32_t sd;
  if (!parse_sd(sd_str, &sd))
    ERROR_MSG_RET("invalid sd '%s' (decimal or 0x-prefixed hex, 0-0xFFFFFF)\n", sd_str);

  gNB_MAC_INST *mac = RC.nrmac[0];
  AssertFatal(mac != NULL, "need MAC\n");

  nssai_t nssai = {.sst = sst, .sd = sd};
  NR_SCHED_LOCK(&mac->sched_lock);
  int removed = nr_slicing_remove_slice(&mac->slice_config, nssai);
  NR_SCHED_UNLOCK(&mac->sched_lock);

  if (!removed)
    ERROR_MSG_RET("no slice with nssai=%d.0x%06x found\n", sst, sd);
  prnt("OK: removed slice nssai=%d.0x%06x\n", sst, sd);
  return 0;
}

// Lists currently configured slices and their algo/params ("slice_list", no arguments).
static int slice_list(char *buf, int debug, telnet_printfunc_t prnt)
{
  UNUSED(debug);
  if (buf)
    ERROR_MSG_RET("no parameter allowed\n");

  gNB_MAC_INST *mac = RC.nrmac[0];
  AssertFatal(mac != NULL, "need MAC\n");

  NR_SCHED_LOCK(&mac->sched_lock);
  int num = mac->slice_config.num;
  const char *algo_name =
      mac->dl_slice_algo == nr_dl_rrm_ratio ? "rrm" : mac->dl_slice_algo == nr_dl_nvs ? "nvs" : mac->dl_slice_algo ? "custom" : "none";
  prnt("%d slice(s) configured (dl_slice_algo %s):\n", num, algo_name);
  for (int i = 0; i < num; i++) {
    const nr_slice_t *s = &mac->slice_config.s[i];
    prnt("  [%d] nssai=%d.0x%06x label=%s", i, s->nssai.sst, s->nssai.sd, s->label ? s->label : "?");
    if (mac->dl_slice_algo == nr_dl_rrm_ratio && s->algo_data) {
      const nr_slice_rrm_ratio_params_t *p = s->algo_data;
      prnt(" dedicated=%d min=%d max=%d", p->dedicated_ratio, p->min_ratio, p->max_ratio);
    } else if (mac->dl_slice_algo == nr_dl_nvs && s->algo_data) {
      const nr_slice_nvs_params_t *p = s->algo_data;
      prnt(" pct_reserved=%d", p->pct_reserved);
    }
    prnt("\n");
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);
  prnt("OK\n");
  return 0;
}

static telnetshell_cmddef_t cicmds[] = {
    {"get_single_rnti", "", get_single_rnti},
    {"force_reestab", "[rnti(hex,opt)]", trigger_reestab},
    {"get_reestab_count", "[rnti(hex,opt)]", get_reestab_count},
    {"force_ue_release", "[rnti(hex,opt)]", force_ue_release},
    {"force_ul_failure", "[rnti(hex,opt)]", force_ul_failure},
    {"trigger_f1_ho", "[rrc_ue_id(int,opt)]", rrc_gNB_trigger_f1_ho},
    {"fetch_du_by_ue_id", "[rrc_ue_id(int,opt)]", fetch_du_by_ue_id},
    {"get_current_bwp", "[rnti(hex,opt)]", get_current_bwp},
    {"trigger_bwp_switch", "newBWPId [rnti(hex,opt)]", trigger_bwp_switch},
    {"trigger_n2_ho", "[neighbour_pci(uint32_t),ueId(uint32_t)]", rrc_gNB_trigger_n2_ho},
    {"set_pusch_target_snr", "[somelongSNR(dec)]", set_pusch_target_snr},
    {"pdu_session_release", "[gNB_ue_ngap_id(int,opt)]", trigger_ngap_pdu_session_release},
    {"slice_add", "rrm|nvs <sst> <sd> <label> <dedicated> <min> <max>|<pct_reserved>", slice_add},
    {"slice_remove", "<sst> <sd>", slice_remove},
    {"slice_list", "", slice_list},
    {"", "", NULL},
};

static telnetshell_vardef_t civars[] = {

  {"", 0, 0, NULL}
};

void add_ci_cmds(void) {
  add_telnetcmd("ci", civars, cicmds);
}
