/*
 * Copyright (c) 2004 Marcel Moolenaar
 * Copyright (c) 2005 David Xu
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <proc_service.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <thread_db.h>
#include <unistd.h>

#include "thread_db_int.h"

struct pt_map {
	int		used;
	lwpid_t		lwp;
	psaddr_t	thr;
};

struct td_thragent {
	TD_THRAGENT_FIELDS;
	psaddr_t	libthr_debug_addr;
	psaddr_t	thread_list_addr;
	psaddr_t	thread_listgen_addr;
	psaddr_t	thread_active_threads_addr;
	psaddr_t	thread_keytable_addr;
	int		thread_inited;
	int		thread_off_dtv;
	int		thread_off_tlsindex;
	int		thread_off_attr_flags;
	int		thread_size_key;
	int		thread_off_tcb;
	int		thread_off_linkmap;
	int		thread_off_thr_locklevel;
	int		thread_off_next;
	int		thread_off_state;
	int		thread_off_isdead;
	int		thread_off_tid;
	int		thread_max_keys;
	int		thread_off_key_allocated;
	int		thread_off_key_destructor;
	int		thread_state_zoombie;
	int		thread_state_running;
	struct pt_map	*map;
	int		map_len;
};

#define P2T(c) ps2td(c)

static void pt_unmap_lwp(const td_thragent_t *ta, lwpid_t lwp);
static int pt_validate(const td_thrhandle_t *th);

static int
ps2td(int c)
{
	switch (c) {
	case PS_OK:
		return TD_OK;
	case PS_ERR:
		return TD_ERR;
	case PS_BADPID:
		return TD_BADPH;
	case PS_BADLID:
		return TD_NOLWP;
	case PS_BADADDR:
		return TD_ERR;
	case PS_NOSYM:
		return TD_NOLIBTHREAD;
	case PS_NOFREGS:
		return TD_NOFPREGS;
	default:
		return TD_ERR;
	}
}

static long
pt_map_thread(const td_thragent_t *const_ta, long lwp, psaddr_t pt)
{
	td_thragent_t *ta = __DECONST(td_thragent_t *, const_ta);
	struct pt_map *new;
	int i, first = -1;

	/* leave zero out */
	for (i = 1; i < ta->map_len; ++i) {
		if (ta->map[i].used == 0) {
			if (first == -1)
				first = i;
		} else if (ta->map[i].lwp == lwp) {
			ta->map[i].thr = pt;
			return (i);
		}
	}

	if (first == -1) {
		if (ta->map_len == 0) {
			ta->map = calloc(20, sizeof(struct pt_map));
			if (ta->map == NULL)
				return (-1);
			ta->map_len = 20;
			first = 1;
		} else {
			new = realloc(ta->map,
			              sizeof(struct pt_map) * ta->map_len * 2);
			if (new == NULL)
				return (-1);
			memset(new + ta->map_len, '\0', sizeof(struct pt_map) *
			       ta->map_len);
			first = ta->map_len;
			ta->map = new;
			ta->map_len *= 2;
		}
	}

	ta->map[first].used = 1;
	ta->map[first].thr  = pt;
	ta->map[first].lwp  = lwp;
	return (first);
}

static td_err_e
pt_init(void)
{
	return (0);
}

static td_err_e
pt_ta_new(struct ps_prochandle *ph, td_thragent_t **pta)
{
#define LOOKUP_SYM(proc, sym, addr) 			\
	ret = ps_pglobal_lookup(proc, NULL, sym, addr);	\
	if (ret != 0) {					\
		TDBG("can not find symbol: %s\n", sym);	\
		ret = TD_NOLIBTHREAD;			\
		goto error;				\
	}

#define	LOOKUP_VAL(proc, sym, val)			\
	ret = ps_pglobal_lookup(proc, NULL, sym, &vaddr);\
	if (ret != 0) {					\
		TDBG("can not find symbol: %s\n", sym);	\
		ret = TD_NOLIBTHREAD;			\
		goto error;				\
	}						\
	ret = ps_pread(proc, vaddr, val, sizeof(int));	\
	if (ret != 0) {					\
		TDBG("can not read value of %s\n", sym);\
		ret = TD_NOLIBTHREAD;			\
		goto error;				\
	}

	td_thragent_t *ta;
	psaddr_t vaddr;
	int dbg;
	int ret;

	TDBG_FUNC();

	ta = malloc(sizeof(td_thragent_t));
	if (ta == NULL)
		return (TD_MALLOC);

	ta->ph = ph;
	ta->map = NULL;
	ta->map_len = 0;

	LOOKUP_SYM(ph, "_libthr_debug",		&ta->libthr_debug_addr);
	LOOKUP_SYM(ph, "_thread_list",		&ta->thread_list_addr);
	LOOKUP_SYM(ph, "_thread_active_threads",&ta->thread_active_threads_addr);
	LOOKUP_SYM(ph, "_thread_keytable",	&ta->thread_keytable_addr);
	LOOKUP_VAL(ph, "_thread_off_dtv",	&ta->thread_off_dtv);
	LOOKUP_VAL(ph, "_thread_off_tlsindex",	&ta->thread_off_tlsindex);
	LOOKUP_VAL(ph, "_thread_off_attr_flags",	&ta->thread_off_attr_flags);
	LOOKUP_VAL(ph, "_thread_size_key",	&ta->thread_size_key);
	LOOKUP_VAL(ph, "_thread_off_tcb",	&ta->thread_off_tcb);
	LOOKUP_VAL(ph, "_thread_off_tid",	&ta->thread_off_tid);
	LOOKUP_VAL(ph, "_thread_off_linkmap",	&ta->thread_off_linkmap);
	LOOKUP_VAL(ph, "_thread_off_thr_locklevel",	&ta->thread_off_thr_locklevel);
	LOOKUP_VAL(ph, "_thread_off_next",	&ta->thread_off_next);
	LOOKUP_VAL(ph, "_thread_off_state",	&ta->thread_off_state);
	LOOKUP_VAL(ph, "_thread_off_isdead",	&ta->thread_off_isdead);
	LOOKUP_VAL(ph, "_thread_max_keys",	&ta->thread_max_keys);
	LOOKUP_VAL(ph, "_thread_off_key_allocated", &ta->thread_off_key_allocated);
	LOOKUP_VAL(ph, "_thread_off_key_destructor", &ta->thread_off_key_destructor);
	LOOKUP_VAL(ph, "_thread_state_running", &ta->thread_state_running);
	LOOKUP_VAL(ph, "_thread_state_zoombie", &ta->thread_state_zoombie);
	dbg = getpid();
	/*
	 * If this fails it probably means we're debugging a core file and
	 * can't write to it.
	 */
	ps_pwrite(ph, ta->libthr_debug_addr, &dbg, sizeof(int));
	*pta = ta;
	return (0);

error:
	free(ta);
	return (ret);
}

static td_err_e
pt_ta_delete(td_thragent_t *ta)
{
	int dbg;

	TDBG_FUNC();

	dbg = 0;
	/*
	 * Error returns from this write are not really a problem;
	 * the process doesn't exist any more.
	 */
	ps_pwrite(ta->ph, ta->libthr_debug_addr, &dbg, sizeof(int));
	if (ta->map)
		free(ta->map);
	free(ta);
	return (TD_OK);
}

static td_err_e
pt_ta_map_id2thr(const td_thragent_t *ta, thread_t id, td_thrhandle_t *th)
{
	prgregset_t gregs;
	TAILQ_HEAD(, pthread) thread_list;
	psaddr_t pt;
	int ret, isdead;

	TDBG_FUNC();

	if (id < 0 || id >= ta->map_len || ta->map[id].used == 0)
		return (TD_NOTHR);

	if (ta->map[id].thr == NULL) {
		/* check lwp */
		ret = ptrace(PT_GETREGS, ta->map[id].lwp, (caddr_t)&gregs, 0);
		if (ret != 0) {
			/* no longer exists */
			ta->map[id].used = 0;
			return (TD_NOTHR);
		}
	} else {
		ret = ps_pread(ta->ph, ta->thread_list_addr, &thread_list,
		       sizeof(thread_list));
		if (ret != 0)
			return (P2T(ret));
		pt = (psaddr_t)thread_list.tqh_first;
		while (pt != 0 && ta->map[id].thr != pt) {
			/* get next thread */
			ret = ps_pread(ta->ph,
				pt + ta->thread_off_next,
				&pt, sizeof(pt));
			if (ret != 0)
				return (P2T(ret));
		}
		if (pt == 0) {
			/* no longer exists */
			ta->map[id].used = 0;
			return (TD_NOTHR);
		}
		ret = ps_pread(ta->ph,
			pt + ta->thread_off_isdead,
			&isdead, sizeof(isdead));
		if (ret != 0)
			return (P2T(ret));
		if (isdead) {
			ta->map[id].used = 0;
			return (TD_NOTHR);
		}
	}
	th->th_ta  = ta;
	th->th_tid = id;
	th->th_thread = pt;
	return (TD_OK);
}

static td_err_e
pt_ta_map_lwp2thr(const td_thragent_t *ta, lwpid_t lwp, td_thrhandle_t *th)
{
	TAILQ_HEAD(, pthread) thread_list;
	psaddr_t pt;
	long tmp_lwp; 
	int ret;
	
	TDBG_FUNC();

	ret = ps_pread(ta->ph, ta->thread_list_addr, &thread_list,
	               sizeof(thread_list));
	if (ret != 0)
		return (P2T(ret));
	pt = (psaddr_t)thread_list.tqh_first;
	while (pt != 0) {
		ret = ps_pread(ta->ph, pt + ta->thread_off_tid,
			       &tmp_lwp, sizeof(tmp_lwp));
		if (ret != 0)
			return (P2T(ret));
		if (tmp_lwp == (long)lwp)
			break;

		/* get next thread */
		ret = ps_pread(ta->ph,
		           pt + ta->thread_off_next,
		           &pt, sizeof(pt));
		if (ret != 0)
			return (P2T(ret));
	}
	if (pt == 0)
		return (TD_NOTHR);
	th->th_ta  = ta;
	th->th_tid = pt_map_thread(ta, lwp, pt);
	th->th_thread = pt;
	if (th->th_tid == -1)
		return (TD_MALLOC);
	return (TD_OK);
}

static td_err_e
pt_ta_thr_iter(const td_thragent_t *ta,
               td_thr_iter_f *callback, void *cbdata_p,
               td_thr_state_e state, int ti_pri,
               sigset_t *ti_sigmask_p,
               unsigned int ti_user_flags)
{
	TAILQ_HEAD(, pthread) thread_list;
	td_thrhandle_t th;
	long tmp_lwp;
	psaddr_t pt;
	int ret, isdead;

	TDBG_FUNC();

	ret = ps_pread(ta->ph, ta->thread_list_addr, &thread_list,
		       sizeof(thread_list));
	if (ret != 0)
		return (P2T(ret));

	pt = (psaddr_t)thread_list.tqh_first;
	while (pt != 0) {
		ret = ps_pread(ta->ph, pt + ta->thread_off_isdead, &isdead,
			sizeof(isdead));
		if (ret != 0)
			return (P2T(ret));
		if (!isdead) {
			ret = ps_pread(ta->ph, pt + ta->thread_off_tid, &tmp_lwp, 
				      sizeof(tmp_lwp));
			if (ret != 0)
				return (P2T(ret));
			if (tmp_lwp != 0) {
				th.th_ta  = ta;
				th.th_tid = pt_map_thread(ta, tmp_lwp, pt);
				th.th_thread = pt;
				if (th.th_tid == -1)
					return (TD_MALLOC);
				if ((*callback)(&th, cbdata_p))
					return (TD_DBERR);
			}
		}
		/* get next thread */
		ret = ps_pread(ta->ph, pt + ta->thread_off_next, &pt,
			       sizeof(pt));
		if (ret != 0)
			return (P2T(ret));
	}
	return (TD_OK);
}

static td_err_e
pt_ta_tsd_iter(const td_thragent_t *ta, td_key_iter_f *ki, void *arg)
{
	char *keytable;
	void *destructor;
	int i, ret, allocated;

	TDBG_FUNC();

	keytable = malloc(ta->thread_max_keys * ta->thread_size_key);
	if (keytable == NULL)
		return (TD_MALLOC);
	ret = ps_pread(ta->ph, (psaddr_t)ta->thread_keytable_addr, keytable,
	               ta->thread_max_keys * ta->thread_size_key);
	if (ret != 0) {
		free(keytable);
		return (P2T(ret));
	}	
	for (i = 0; i < ta->thread_max_keys; i++) {
		allocated = *(int *)(keytable + i * ta->thread_size_key +
			ta->thread_off_key_allocated);
		destructor = *(void **)(keytable + i * ta->thread_size_key +
			ta->thread_off_key_destructor);
		if (allocated) {
			ret = (ki)(i, destructor, arg);
			if (ret != 0) {
				free(keytable);
				return (TD_DBERR);
			}
		}
	}
	free(keytable);
	return (TD_OK);
}

static td_err_e
pt_ta_event_addr(const td_thragent_t *ta, td_event_e event, td_notify_t *ptr)
{
	TDBG_FUNC();
	return (TD_NOEVENT);
}

static td_err_e
pt_ta_set_event(const td_thragent_t *ta, td_thr_events_t *events)
{
	TDBG_FUNC();
	return (TD_ERR);
}

static td_err_e
pt_ta_clear_event(const td_thragent_t *ta, td_thr_events_t *events)
{
	TDBG_FUNC();
	return (TD_ERR);
}

static td_err_e
pt_ta_event_getmsg(const td_thragent_t *ta, td_event_msg_t *msg)
{
	TDBG_FUNC();
	return (TD_NOMSG);
}

static td_err_e
pt_dbsuspend(const td_thrhandle_t *th, int suspend)
{
	td_thragent_t *ta = (td_thragent_t *)th->th_ta;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	if (suspend)
		ret = ps_lstop(ta->ph, ta->map[th->th_tid].lwp);
	else
		ret = ps_lcontinue(ta->ph, ta->map[th->th_tid].lwp);
	return (P2T(ret));
}

static td_err_e
pt_thr_dbresume(const td_thrhandle_t *th)
{
	TDBG_FUNC();

	return pt_dbsuspend(th, 0);
}

static td_err_e
pt_thr_dbsuspend(const td_thrhandle_t *th)
{
	TDBG_FUNC();

	return pt_dbsuspend(th, 1);
}

static td_err_e
pt_thr_validate(const td_thrhandle_t *th)
{
	td_thrhandle_t temp;
	int ret;

	TDBG_FUNC();

	ret = pt_ta_map_id2thr(th->th_ta, th->th_tid,
	                       &temp);
	return (ret);
}

static td_err_e
pt_thr_get_info(const td_thrhandle_t *th, td_thrinfo_t *info)
{
	const td_thragent_t *ta = th->th_ta;
	int state;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	memset(info, 0, sizeof(*info));
	if (ta->map[th->th_tid].thr == 0) {
		info->ti_type = TD_THR_SYSTEM;
		info->ti_lid = ta->map[th->th_tid].lwp;
		info->ti_tid = th->th_tid;
		info->ti_state = TD_THR_RUN;
		info->ti_type = TD_THR_SYSTEM;
		info->ti_thread = NULL;
		return (TD_OK);
	}
	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr + ta->thread_off_state,
	               &state, sizeof(state));
	if (ret != 0)
		return (P2T(ret));
	info->ti_lid = ta->map[th->th_tid].lwp;
	info->ti_tid = th->th_tid;
	info->ti_thread = ta->map[th->th_tid].thr;
	info->ti_ta_p = th->th_ta;
	if (state == ta->thread_state_running)
		info->ti_state = TD_THR_RUN;
	else if (state == ta->thread_state_zoombie)
		info->ti_state = TD_THR_ZOMBIE;
	else
		info->ti_state = TD_THR_SLEEP;
	info->ti_type = TD_THR_USER;
	return (0);
}

static td_err_e
pt_thr_getfpregs(const td_thrhandle_t *th, prfpregset_t *fpregs)
{
	const td_thragent_t *ta = th->th_ta;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	ret = ps_lgetfpregs(ta->ph, ta->map[th->th_tid].lwp, fpregs);
	return (P2T(ret));
}

static td_err_e
pt_thr_getgregs(const td_thrhandle_t *th, prgregset_t gregs)
{
	const td_thragent_t *ta = th->th_ta;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	ret = ps_lgetregs(ta->ph,
	                  ta->map[th->th_tid].lwp, gregs);
	return (P2T(ret));
}

static td_err_e
pt_thr_setfpregs(const td_thrhandle_t *th, const prfpregset_t *fpregs)
{
	const td_thragent_t *ta = th->th_ta;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	ret = ps_lsetfpregs(ta->ph, ta->map[th->th_tid].lwp, fpregs);
	return (P2T(ret));
}

static td_err_e
pt_thr_setgregs(const td_thrhandle_t *th, const prgregset_t gregs)
{
	const td_thragent_t *ta = th->th_ta;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	ret = ps_lsetregs(ta->ph, ta->map[th->th_tid].lwp, gregs);
	return (P2T(ret));
}

static td_err_e
pt_thr_event_enable(const td_thrhandle_t *th, int en)
{
	TDBG_FUNC();
	return (TD_ERR);
}

static td_err_e
pt_thr_set_event(const td_thrhandle_t *th, td_thr_events_t *setp)
{
	TDBG_FUNC();
	return (TD_ERR);
}

static td_err_e
pt_thr_clear_event(const td_thrhandle_t *th, td_thr_events_t *setp)
{
	TDBG_FUNC();
	return (TD_ERR);
}

static td_err_e
pt_thr_event_getmsg(const td_thrhandle_t *th, td_event_msg_t *msg)
{
	TDBG_FUNC();
	return (TD_NOMSG);
}

static td_err_e
pt_thr_sstep(const td_thrhandle_t *th, int step)
{
	const td_thragent_t *ta = th->th_ta;
	int ret;

	TDBG_FUNC();

	ret = pt_validate(th);
	if (ret)
		return (ret);

	if (ta->map[th->th_tid].thr == 0)
		return (TD_BADTH);

	return (0);
}

static int
pt_validate(const td_thrhandle_t *th)
{

	if (th->th_tid <= 0 || th->th_tid >= th->th_ta->map_len ||
	    th->th_ta->map[th->th_tid].used == 0)
		return (TD_NOTHR);
	return (TD_OK);
}

static td_err_e
pt_thr_tls_get_addr(const td_thrhandle_t *th, void *_linkmap, size_t offset,
		    void **address)
{
	char *obj_entry;
	const td_thragent_t *ta = th->th_ta;
	psaddr_t tcb_addr, *dtv_addr, tcb_tp;
	int tls_index, ret;

	/* linkmap is a member of Obj_Entry */
	obj_entry = (char *)_linkmap - ta->thread_off_linkmap;

	/* get tlsindex of the object file */
	ret = ps_pread(ta->ph,
		obj_entry + ta->thread_off_tlsindex,
		&tls_index, sizeof(tls_index));
	if (ret != 0)
		return (P2T(ret));

	/* get thread tcb */
	ret = ps_pread(ta->ph, ta->map[th->th_tid].thr +
		ta->thread_off_tcb,
		&tcb_addr, sizeof(tcb_addr));
	if (ret != 0)
		return (P2T(ret));

	/* get dtv array address */
	ret = ps_pread(ta->ph, tcb_addr + ta->thread_off_dtv,
		&dtv_addr, sizeof(dtv_addr));
	if (ret != 0)
		return (P2T(ret));
	/* now get the object's tls block base address */
	ret = ps_pread(ta->ph, &dtv_addr[tls_index+1], address,
		sizeof(*address));
	if (ret != 0)
		return (P2T(ret));

	*address += offset;
	return (TD_OK);
}

struct ta_ops libthr_db_ops = {
	.to_init		= pt_init,
	.to_ta_clear_event	= pt_ta_clear_event,
	.to_ta_delete		= pt_ta_delete,
	.to_ta_event_addr	= pt_ta_event_addr,
	.to_ta_event_getmsg	= pt_ta_event_getmsg,
	.to_ta_map_id2thr	= pt_ta_map_id2thr,
	.to_ta_map_lwp2thr	= pt_ta_map_lwp2thr,
	.to_ta_new		= pt_ta_new,
	.to_ta_set_event	= pt_ta_set_event,
	.to_ta_thr_iter		= pt_ta_thr_iter,
	.to_ta_tsd_iter		= pt_ta_tsd_iter,
	.to_thr_clear_event	= pt_thr_clear_event,
	.to_thr_dbresume	= pt_thr_dbresume,
	.to_thr_dbsuspend	= pt_thr_dbsuspend,
	.to_thr_event_enable	= pt_thr_event_enable,
	.to_thr_event_getmsg	= pt_thr_event_getmsg,
	.to_thr_get_info	= pt_thr_get_info,
	.to_thr_getfpregs	= pt_thr_getfpregs,
	.to_thr_getgregs	= pt_thr_getgregs,
	.to_thr_set_event	= pt_thr_set_event,
	.to_thr_setfpregs	= pt_thr_setfpregs,
	.to_thr_setgregs	= pt_thr_setgregs,
	.to_thr_validate	= pt_thr_validate,
	.to_thr_tls_get_addr	= pt_thr_tls_get_addr,

	/* FreeBSD specific extensions. */
	.to_thr_sstep		= pt_thr_sstep,
};
