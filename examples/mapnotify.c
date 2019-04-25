/*
 * Copyright (c) 2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse <jfriesse@redhat.com>
 *         Angus Salkeld <asalkeld@redhat.com>
 *
 * libqb is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * libqb is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libqb.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "os_base.h"

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qbmap.h>

static void
notify_fn(uint32_t event, char *key, void *old_value, void *value,
	  void *user_data)
{
	if (event == QB_MAP_NOTIFY_FREE) {
		fprintf(stderr, "Notify[FREE] %s [%d]\n",
			key, *(int *)old_value);
		free(old_value);
	} else if (event == QB_MAP_NOTIFY_DELETED) {
		fprintf(stderr, "Notify[DELETED] %s [%d]\n",
			key, *(int *)old_value);
	} else if (event == QB_MAP_NOTIFY_REPLACED) {
		fprintf(stderr, "Notify[REPLACED] %s [%d] -> [%d]\n",
			key, *(int *)old_value, *(int *)value);
	} else {
		fprintf(stderr, "Notify[%" PRIu32 "] %s \n", event, key);
		if (value != NULL) {
			fprintf(stderr, " value = [%d]\n", *(int *)value);
		}
		if (old_value != NULL) {
			fprintf(stderr, " old value = [%d]\n",
				*(int *)old_value);
		}
	}
}

static void
add_cs_keys(qb_map_t * m)
{
	qb_map_put(m, "compatibility", strdup("none"));
	qb_map_put(m, "totem.version", strdup("2"));
	qb_map_put(m, "totem.secauth", strdup("off"));
	qb_map_put(m, "totem.threads", strdup("0"));
	qb_map_put(m, "totem.interface.ringnumber", strdup("0"));
	qb_map_put(m, "totem.interface.bindnetaddr", strdup("192.168.122.1"));
	qb_map_put(m, "totem.interface.mcastaddr", strdup("239.255.1.1"));
	qb_map_put(m, "totem.interface.mcastport", strdup("5405"));
	qb_map_put(m, "totem.interface.ttl", strdup("1"));
	qb_map_put(m, "logging.to_stderr", strdup("yes"));
	qb_map_put(m, "logging.to_logfile", strdup("no"));
	qb_map_put(m, "logging.logfile", strdup("/var/log/cluster/corosync.log"));
	qb_map_put(m, "logging.to_syslog", strdup("no"));
	qb_map_put(m, "logging.debug", strdup("off"));
	qb_map_put(m, "logging.timestamp", strdup("on"));
	qb_map_put(m, "logging.logger_subsys.subsys", strdup("MAIN"));
	qb_map_put(m, "logging.logger_subsys.debug", strdup("on"));
	qb_map_put(m, "amf.mode", strdup("disabled"));
	qb_map_put(m, "quorum.provider", strdup("corosync_quorum_ykd"));
	qb_map_put(m, "runtime.services.evs.service_id", strdup("0"));
	qb_map_put(m, "runtime.services.evs.0.tx", strdup("0"));
	qb_map_put(m, "runtime.services.evs.0.rx", strdup("0"));
	qb_map_put(m, "runtime.services.cfg.service_id", strdup("7"));
	qb_map_put(m, "runtime.services.cfg.0.tx", strdup("0"));
	qb_map_put(m, "runtime.services.cfg.0.rx", strdup("0"));
	qb_map_put(m, "runtime.services.cfg.1.tx", strdup("0"));
	qb_map_put(m, "runtime.services.cfg.1.rx", strdup("0"));
	qb_map_put(m, "runtime.services.cfg.2.tx", strdup("0"));
	qb_map_put(m, "runtime.services.cfg.2.rx", strdup("0"));
	qb_map_put(m, "runtime.services.cfg.3.tx", strdup("0"));
	qb_map_put(m, "runtime.services.cfg.3.rx", strdup("0"));
	qb_map_put(m, "runtime.services.cpg.service_id", strdup("8"));
	qb_map_put(m, "runtime.services.cpg.0.tx", strdup("0"));
	qb_map_put(m, "runtime.services.cpg.0.rx", strdup("0"));
	qb_map_put(m, "runtime.services.cpg.1.tx", strdup("0"));
	qb_map_put(m, "runtime.services.cpg.1.rx", strdup("0"));
	qb_map_put(m, "runtime.services.cpg.2.tx", strdup("0"));
	qb_map_put(m, "runtime.services.cpg.2.rx", strdup("0"));
	qb_map_put(m, "runtime.services.cpg.3.tx", strdup("0"));
	qb_map_put(m, "runtime.services.cpg.3.rx", strdup("0"));
	qb_map_put(m, "runtime.services.cpg.4.tx", strdup("0"));
	qb_map_put(m, "runtime.services.cpg.4.rx", strdup("0"));
	qb_map_put(m, "runtime.services.cpg.5.tx", strdup("1"));
	qb_map_put(m, "runtime.services.cpg.5.rx", strdup("1"));
	qb_map_put(m, "runtime.services.confdb.service_id", strdup("11"));
	qb_map_put(m, "runtime.services.pload.service_id", strdup("13"));
	qb_map_put(m, "runtime.services.pload.0.tx", strdup("0"));
	qb_map_put(m, "runtime.services.pload.0.rx", strdup("0"));
	qb_map_put(m, "runtime.services.pload.1.tx", strdup("0"));
	qb_map_put(m, "runtime.services.pload.1.rx", strdup("0"));
	qb_map_put(m, "runtime.services.quorum.service_id", strdup("12"));
	qb_map_put(m, "runtime.connections.active", strdup("1"));
	qb_map_put(m, "runtime.connections.closed", strdup("0"));
	qb_map_put(m, "runtime.connections.corosync-objctl:24175:0x17fd2b0.service_id", strdup("0"));
	qb_map_put(m, "runtime.connections.corosync-objctl:24175:0x17fd2b0.client_pid", strdup("0"));
	qb_map_put(m, "runtime.connections.corosync-objctl:24175:0x17fd2b0.responses", strdup("0"));
	qb_map_put(m, "runtime.connections.corosync-objctl:24175:0x17fd2b0.dispatched", strdup("0"));
	qb_map_put(m, "runtime.connections.corosync-objctl:24175:0x17fd2b0.requests", strdup("0"));
	qb_map_put(m, "runtime.connections.corosync-objctl:24175:0x17fd2b0.send_retries", strdup("0"));
	qb_map_put(m, "runtime.connections.corosync-objctl:24175:0x17fd2b0.recv_retries", strdup("0"));
	qb_map_put(m, "runtime.connections.corosync-objctl:24175:0x17fd2b0.flow_control", strdup("0"));
	qb_map_put(m, "runtime.connections.corosync-objctl:24175:0x17fd2b0.flow_control_count", strdup("0"));
	qb_map_put(m, "runtime.connections.corosync-objctl:24175:0x17fd2b0.queue_size", strdup("0"));
	qb_map_put(m, "runtime.connections.corosync-objctl:24175:0x17fd2b0.invalid_request", strdup("0"));
	qb_map_put(m, "runtime.connections.corosync-objctl:24175:0x17fd2b0.overload", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.msg_reserved", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.msg_queue_avail", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.orf_token_tx", strdup("1"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.orf_token_rx", strdup("100"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.memb_merge_detect_tx", strdup("29"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.memb_merge_detect_rx", strdup("29"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.memb_join_tx", strdup("1"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.memb_join_rx", strdup("1"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.mcast_tx", strdup("13"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.mcast_retx", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.mcast_rx", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.memb_commit_token_tx", strdup("2"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.memb_commit_token_rx", strdup("2"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.token_hold_cancel_tx", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.token_hold_cancel_rx", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.operational_entered", strdup("1"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.operational_token_lost", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.gather_entered", strdup("1"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.gather_token_lost", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.commit_entered", strdup("1"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.commit_token_lost", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.recovery_entered", strdup("1"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.recovery_token_lost", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.consensus_timeouts", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.mtt_rx_token", strdup("106"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.avg_token_workload", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.avg_backlog_calc", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.rx_msg_dropped", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.continuous_gather", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.firewall_enabled_or_nic_failure", strdup("0"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.members.24815808.ip", strdup("r(0) ip(192.168.122.1) "));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.members.24815808.join_count", strdup("1"));
	qb_map_put(m, "runtime.totem.pg.mrp.srp.members.24815808.status", strdup("joined"));
	qb_map_put(m, "runtime.blackbox.dump_flight_data", strdup("no"));
	qb_map_put(m, "runtime.blackbox.dump_state", strdup("no"));
}


int
main(void)
{
	qb_map_t *trie;
	int *i1, *i2, *i3;
	qb_map_iter_t *iter;
	const char *key;
	void *val;
	uint32_t revents = (QB_MAP_NOTIFY_DELETED |
			    QB_MAP_NOTIFY_REPLACED |
			    QB_MAP_NOTIFY_INSERTED |
			    QB_MAP_NOTIFY_RECURSIVE);

	trie = qb_trie_create();
	assert(trie != NULL);
	qb_trie_dump(trie);
	add_cs_keys(trie);

	i1 = malloc(sizeof(int));
	assert(i1 != NULL);
	*i1 = 1;

	i2 = malloc(sizeof(int));
	assert(i2 != NULL);
	*i2 = 2;

	i3 = malloc(sizeof(int));
	assert(i3 != NULL);
	*i3 = 3;

	qb_map_notify_add(trie, NULL, notify_fn, QB_MAP_NOTIFY_FREE, NULL);

	qb_map_put(trie, "test.key1", i1);
	qb_map_put(trie, "test.key2", i2);

	qb_map_notify_add(trie, "test.", notify_fn, revents, NULL);
	qb_trie_dump(trie);

	qb_map_put(trie, "test.key1", i3);

	iter = qb_map_pref_iter_create(trie, "test.");
	while ((key = qb_map_iter_next(iter, &val)) != NULL) {
		fprintf(stderr, "Iter %s [%d]\n", key, *(int *)val);
		qb_map_rm(trie, key);
	}
	qb_map_iter_free(iter);
	qb_map_notify_del_2(trie, "test.", notify_fn, revents, NULL);
	qb_map_destroy(trie);

	return (0);
}
