# Copyright (C) 2010 Red Hat, Inc.
#
# Authors: Angus Salkeld <asalkeld@redhat.com>
#
# This file is part of libqb.
#
# libqb is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 2.1 of the License, or
# (at your option) any later version.
#
# libqb is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with libqb.  If not, see <http://www.gnu.org/licenses/>.

MAINTAINERCLEANFILES	= Makefile.in
EXTRA_DIST 		= man.dox.in
noinst_HEADERS          = mainpage.h

dist_man_MANS = man8/qb-blackbox.8
if HAVE_DOXYGEN
if BUILD_MAN

doxygen2man_flags = -q -i qb/ -o man3/ -m -g -P -H "libqb Programmer's Manual" $$($(UTC_DATE_AT)$(SOURCE_EPOCH) +"-D %F -Y %Y") \
		    -p LIBQB -c -O ../include/qb -Y $(SOURCE_EPOCH)

man3_MANS = \
	man3/qb_array_create_2.3 \
	man3/qb_array_create.3 \
	man3/qb_array_elems_per_bin_get.3 \
	man3/qb_array_free.3 \
	man3/qb_array_grow.3 \
	man3/qbarray.h.3 \
	man3/qb_array_index.3 \
	man3/qb_array_new_bin_cb_set.3 \
	man3/qb_array_num_bins_get.3 \
	man3/qbatomic.h.3 \
	man3/qb_atomic_init.3 \
	man3/qb_atomic_int_add.3 \
	man3/qb_atomic_int_compare_and_exchange.3 \
	man3/qb_atomic_int_exchange_and_add.3 \
	man3/qb_atomic_int_get.3 \
	man3/qb_atomic_int_set.3 \
	man3/qb_atomic_pointer_compare_and_exchange.3 \
	man3/qb_atomic_pointer_get.3 \
	man3/qb_atomic_pointer_set.3 \
	man3/qbdefs.h.3 \
	man3/qb_hashtable_create.3 \
	man3/qb_hdb_base_convert.3 \
	man3/qb_hdb_create.3 \
	man3/qb_hdb_destroy.3 \
	man3/qbhdb.h.3 \
	man3/qb_hdb_handle_create.3 \
	man3/qb_hdb_handle_destroy.3 \
	man3/qb_hdb_handle_get.3 \
	man3/qb_hdb_handle_get_always.3 \
	man3/qb_hdb_handle_put.3 \
	man3/qb_hdb_handle_refcount_get.3 \
	man3/qb_hdb_iterator_next.3 \
	man3/qb_hdb_iterator_reset.3 \
	man3/qb_hdb_nocheck_convert.3 \
	man3/qb_ipcc_connect.3 \
	man3/qb_ipcc_context_get.3 \
	man3/qb_ipcc_context_set.3 \
	man3/qb_ipcc_disconnect.3 \
	man3/qb_ipcc_event_recv.3 \
	man3/qb_ipcc_fc_enable_max_set.3 \
	man3/qb_ipcc_fd_get.3 \
	man3/qb_ipcc_get_buffer_size.3 \
	man3/qbipcc.h.3 \
	man3/qb_ipcc_is_connected.3 \
	man3/qbipc_common.h.3 \
	man3/qb_ipcc_recv.3 \
	man3/qb_ipcc_send.3 \
	man3/qb_ipcc_sendv.3 \
	man3/qb_ipcc_sendv_recv.3 \
	man3/qb_ipcc_verify_dgram_max_msg_size.3 \
	man3/qb_ipcs_connection_auth_set.3 \
	man3/qb_ipcs_connection_first_get.3 \
	man3/qb_ipcs_connection_get_buffer_size.3 \
	man3/qb_ipcs_connection_next_get.3 \
	man3/qb_ipcs_connection_ref.3 \
	man3/qb_ipcs_connection_service_context_get.3 \
	man3/qb_ipcs_connection_stats_get_2.3 \
	man3/qb_ipcs_connection_stats_get.3 \
	man3/qb_ipcs_connection_unref.3 \
	man3/qb_ipcs_context_get.3 \
	man3/qb_ipcs_context_set.3 \
	man3/qb_ipcs_create.3 \
	man3/qb_ipcs_destroy.3 \
	man3/qb_ipcs_disconnect.3 \
	man3/qb_ipcs_enforce_buffer_size.3 \
	man3/qb_ipcs_event_send.3 \
	man3/qb_ipcs_event_sendv.3 \
	man3/qbipcs.h.3 \
	man3/qb_ipcs_poll_handlers_set.3 \
	man3/qb_ipcs_ref.3 \
	man3/qb_ipcs_request_rate_limit.3 \
	man3/qb_ipcs_response_send.3 \
	man3/qb_ipcs_response_sendv.3 \
	man3/qb_ipcs_run.3 \
	man3/qb_ipcs_service_context_get.3 \
	man3/qb_ipcs_service_context_set.3 \
	man3/qb_ipcs_service_id_get.3 \
	man3/qb_ipcs_stats_get.3 \
	man3/qb_ipcs_unref.3 \
	man3/qb_list_add.3 \
	man3/qb_list_add_tail.3 \
	man3/qb_list_del.3 \
	man3/qb_list_empty.3 \
	man3/qblist.h.3 \
	man3/qb_list_init.3 \
	man3/qb_list_is_last.3 \
	man3/qb_list_length.3 \
	man3/qb_list_replace.3 \
	man3/qb_list_splice.3 \
	man3/qb_list_splice_tail.3 \
	man3/qb_log_blackbox_print_from_file.3 \
	man3/qb_log_blackbox_write_to_file.3 \
	man3/qb_log_callsite_get.3 \
	man3/qb_log_callsites_dump.3 \
	man3/qb_log_callsites_register.3 \
	man3/qb_log_ctl2.3 \
	man3/qb_log_ctl.3 \
	man3/qb_log_custom_close.3 \
	man3/qb_log_custom_open.3 \
	man3/qb_log_facility2int.3 \
	man3/qb_log_facility2str.3 \
	man3/qb_log_file_close.3 \
	man3/qb_log_file_open.3 \
	man3/qb_log_file_reopen.3 \
	man3/qb_log_filter_ctl2.3 \
	man3/qb_log_filter_ctl.3 \
	man3/qb_log_filter_fn_set.3 \
	man3/qb_log_fini.3 \
	man3/qb_log_format_set.3 \
	man3/qb_log_from_external_source.3 \
	man3/qb_log_from_external_source_va.3 \
	man3/qblog.h.3 \
	man3/qb_log_init.3 \
	man3/qb_log_real_.3 \
	man3/qb_log_real_va_.3 \
	man3/qb_log_tags_stringify_fn_set.3 \
	man3/qb_log_target_format.3 \
	man3/qb_log_target_user_data_get.3 \
	man3/qb_log_target_user_data_set.3 \
	man3/qb_log_thread_priority_set.3 \
	man3/qb_log_thread_start.3 \
	man3/qb_loop_create.3 \
	man3/qb_loop_destroy.3 \
	man3/qbloop.h.3 \
	man3/qb_loop_job_add.3 \
	man3/qb_loop_job_del.3 \
	man3/qb_loop_poll_add.3 \
	man3/qb_loop_poll_del.3 \
	man3/qb_loop_poll_low_fds_event_set.3 \
	man3/qb_loop_poll_mod.3 \
	man3/qb_loop_run.3 \
	man3/qb_loop_signal_add.3 \
	man3/qb_loop_signal_del.3 \
	man3/qb_loop_signal_mod.3 \
	man3/qb_loop_stop.3 \
	man3/qb_loop_timer_add.3 \
	man3/qb_loop_timer_del.3 \
	man3/qb_loop_timer_expire_time_get.3 \
	man3/qb_loop_timer_expire_time_remaining.3 \
	man3/qb_loop_timer_is_running.3 \
	man3/qb_map_count_get.3 \
	man3/qb_map_destroy.3 \
	man3/qb_map_foreach.3 \
	man3/qb_map_get.3 \
	man3/qbmap.h.3 \
	man3/qb_map_iter_create.3 \
	man3/qb_map_iter_free.3 \
	man3/qb_map_iter_next.3 \
	man3/qb_map_notify_add.3 \
	man3/qb_map_notify_del_2.3 \
	man3/qb_map_notify_del.3 \
	man3/qb_map_pref_iter_create.3 \
	man3/qb_map_put.3 \
	man3/qb_map_rm.3 \
	man3/qb_rb_chmod.3 \
	man3/qb_rb_chown.3 \
	man3/qb_rb_chunk_alloc.3 \
	man3/qb_rb_chunk_commit.3 \
	man3/qb_rb_chunk_peek.3 \
	man3/qb_rb_chunk_read.3 \
	man3/qb_rb_chunk_reclaim.3 \
	man3/qb_rb_chunks_used.3 \
	man3/qb_rb_chunk_write.3 \
	man3/qb_rb_close.3 \
	man3/qb_rb_create_from_file.3 \
	man3/qbrb.h.3 \
	man3/qb_rb_name_get.3 \
	man3/qb_rb_open.3 \
	man3/qb_rb_refcount_get.3 \
	man3/qb_rb_shared_user_data_get.3 \
	man3/qb_rb_space_free.3 \
	man3/qb_rb_space_used.3 \
	man3/qb_rb_write_to_file.3 \
	man3/qb_skiplist_create.3 \
	man3/qb_strerror_r.3 \
	man3/qb_thread_lock.3 \
	man3/qb_thread_lock_create.3 \
	man3/qb_thread_lock_destroy.3 \
	man3/qb_thread_trylock.3 \
	man3/qb_thread_unlock.3 \
	man3/qb_timespec_add_ms.3 \
	man3/qb_trie_create.3 \
	man3/qb_trie_dump.3 \
	man3/qbutil.h.3 \
	man3/qb_util_nano_current_get.3 \
	man3/qb_util_nano_from_epoch_get.3 \
	man3/qb_util_nano_monotonic_hz.3 \
	man3/qb_util_set_log_function.3 \
	man3/qb_util_stopwatch_create.3 \
	man3/qb_util_stopwatch_free.3 \
	man3/qb_util_stopwatch_sec_elapsed_get.3 \
	man3/qb_util_stopwatch_split.3 \
	man3/qb_util_stopwatch_split_ctl.3 \
	man3/qb_util_stopwatch_split_last.3 \
	man3/qb_util_stopwatch_start.3 \
	man3/qb_util_stopwatch_stop.3 \
	man3/qb_util_stopwatch_time_split_get.3 \
	man3/qb_util_stopwatch_us_elapsed_get.3 \
	man3/qb_util_timespec_from_epoch_get.3

$(man3_MANS): txt-man

txt-man: man.dox
	doxygen man.dox

xml-man: txt-man
	mkdir -p man3
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbarray_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbatomic_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbdefs_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbhdb_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbipcc_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbipc__common_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbipcs_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qblist_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qblog_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbloop_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbmap_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbrb_8h.xml
	$(DOXYGEN2MAN) $(doxygen2man_flags) qbutil_8h.xml

all: $(man3_MANS) xml-man

endif
endif

clean-local:
	rm -rf man3 xml
