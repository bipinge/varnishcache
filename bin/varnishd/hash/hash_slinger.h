/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

struct sess;
struct req;
struct objcore;
struct worker;

typedef void hash_init_f(int ac, char * const *av);
typedef void hash_start_f(void);
typedef void hash_prep_f(struct worker *);
typedef struct objhead *hash_lookup_f(struct worker *wrk, const void *digest,
    struct objhead **nobj);
typedef int hash_deref_f(struct objhead *obj);

struct hash_slinger {
	unsigned		magic;
#define SLINGER_MAGIC		0x1b720cba
	const char		*name;
	hash_init_f		*init;
	hash_start_f		*start;
	hash_prep_f		*prep;
	hash_lookup_f		*lookup;
	hash_deref_f		*deref;
};

enum lookup_e {
	HSH_MISS,
	HSH_BUSY,
	HSH_HIT,
	HSH_EXP,
	HSH_EXPBUSY
};

/* cache_hash.c */
struct ban;
void HSH_Cleanup(struct worker *w);
enum lookup_e HSH_Lookup(struct req *, struct objcore **, struct objcore **,
    int always_insert);
void HSH_Ref(struct objcore *o);
void HSH_Init(const struct hash_slinger *slinger);
void HSH_AddString(struct req *, void *ctx, const char *str);
void HSH_Insert(struct worker *, const void *hash, struct objcore *,
    struct ban *);
void HSH_Purge(struct worker *, struct objhead *, double ttl, double grace,
    double keep);
void HSH_config(const char *h_arg);
struct boc *HSH_RefBoc(const struct objcore *);
void HSH_DerefBoc(struct worker *wrk, struct objcore *);
struct objcore *HSH_Private(struct worker *wrk);
void HSH_Abandon(struct objcore *oc);
int HSH_Snipe(const struct worker *, struct objcore *);
void HSH_Kill(struct objcore *);

#ifdef VARNISH_CACHE_CHILD

struct wlist_entry {
	unsigned			magic;
#define WLIST_ENTRY_MAGIC		0xbba98be4

	VTAILQ_ENTRY(wlist_entry)	list;
	struct req			*req;
};

struct wlist {
	unsigned			magic;
#define WLIST_MAGIC			0x40383dfa

	VTAILQ_HEAD(, wlist_entry)	reqs;
	pthread_cond_t			cv;
	unsigned			cv_waiting;
};

struct objhead {
	unsigned		magic;
#define OBJHEAD_MAGIC		0x1b96615d

	int			refcnt;
	struct lock		mtx;
	VTAILQ_HEAD(,objcore)	objcs;
	uint8_t			digest[DIGEST_LEN];

	/* rescheduled / woken up by hsh_rush, protected by mtx */
	struct wlist		*waitinglist;

	/*----------------------------------------------------
	 * The fields below are for the sole private use of
	 * the hash implementation(s).
	 */
	union {
		struct {
			VTAILQ_ENTRY(objhead)	u_n_hoh_list;
			void			*u_n_hoh_head;
		} n;
	} _u;
#define hoh_list _u.n.u_n_hoh_list
#define hoh_head _u.n.u_n_hoh_head
};

void HSH_Fail(struct objcore *);
void HSH_Unbusy(struct worker *, struct objcore *);
void HSH_DeleteObjHead(struct worker *, struct objhead *oh);
int HSH_DerefObjHead(struct worker *, struct objhead **poh);
int HSH_DerefObjCore(struct worker *, struct objcore **ocp, int rushmax);
#endif /* VARNISH_CACHE_CHILD */

extern const struct hash_slinger hsl_slinger;
extern const struct hash_slinger hcl_slinger;
extern const struct hash_slinger hcb_slinger;
