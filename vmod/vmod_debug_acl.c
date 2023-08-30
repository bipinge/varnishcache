/*-
 * Copyright (c) 2012-2019 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// #include "vdef.h"
//#include "vas.h"
#include "cache/cache.h"
#include "vend.h"
#include "vsa.h"
#include "vsb.h"
#include "vsha256.h"
#include "vtcp.h"
#include "vtim.h"
#include "vcc_debug_if.h"

VCL_ACL v_matchproto_(td_debug_null_acl)
xyzzy_null_acl(VRT_CTX)
{

	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	return (NULL);
}

VCL_ACL v_matchproto_(td_debug_acl)
xyzzy_acl(VRT_CTX, VCL_ACL acl)
{

	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	return (acl);
}

VCL_BOOL v_matchproto_(td_debug_match_acl)
xyzzy_match_acl(VRT_CTX, VCL_ACL acl, VCL_IP ip)
{

	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	assert(VSA_Sane(ip));

	return (VRT_acl_match(ctx, acl, ip));
}

/*
 * The code below is more intimate with VSA than anything is supposed to.
 */

struct acl_sweep {
	int			family;
	const uint8_t		*ip0_p;
	const uint8_t		*ip1_p;
	struct suckaddr		*probe;
	uint8_t			*probe_p;
	VCL_INT			step;
	uint64_t		reset;
	uint64_t		that;
	uint64_t		count;
};

static void
reset_sweep(struct acl_sweep *asw)
{
	asw->that = asw->reset;
}

static int
setup_sweep(VRT_CTX, struct acl_sweep *asw, VCL_IP ip0, VCL_IP ip1,
    VCL_INT step)
{
	int fam0, fam1;
	const uint8_t *ptr;

	AN(asw);
	memset(asw, 0, sizeof *asw);

	AN(ip0);
	AN(ip1);
	fam0 = VSA_GetPtr(ip0, &asw->ip0_p);
	fam1 = VSA_GetPtr(ip1, &asw->ip1_p);
	if (fam0 != fam1) {
		VRT_fail(ctx, "IPs have different families (0x%x vs 0x%x)",
		    fam0, fam1);
		return (-1);
	}

	asw->family = fam0;
	if (asw->family == PF_INET) {
		if (memcmp(asw->ip0_p, asw->ip1_p, 4) > 0) {
			VRT_fail(ctx, "Sweep: ipv4.end < ipv4.start");
			return (-1);
		}
		asw->reset = vbe32dec(asw->ip0_p);
	} else {
		if (memcmp(asw->ip0_p, asw->ip1_p, 16) > 0) {
			VRT_fail(ctx, "Sweep: ipv6.end < ipv6.start");
			return (-1);
		}
		asw->reset = vbe64dec(asw->ip0_p + 8);
	}
	asw->that = asw->reset;

	/* Dont try this at home */
	asw->probe = malloc(vsa_suckaddr_len);
	AN(asw->probe);
	memcpy(asw->probe, ip0, vsa_suckaddr_len);
	(void)VSA_GetPtr(asw->probe, &ptr);
	asw->probe_p = ((uint8_t*)(asw->probe)) + (ptr - (uint8_t*)asw->probe);

	asw->step = step;

	return (0);
}

static void
cleanup_sweep(struct acl_sweep *asw)
{
	free(asw->probe);
	memset(asw, 0, sizeof *asw);
}

static int
step_sweep(struct acl_sweep *asw)
{

	AN(asw);
	asw->count++;
	asw->that += asw->step;
	if (asw->family == PF_INET) {
		vbe32enc(asw->probe_p, asw->that);
		return (memcmp(asw->probe_p, asw->ip1_p, 4));
	} else {
		vbe64enc(asw->probe_p + 8, asw->that);
		return (memcmp(asw->probe_p, asw->ip1_p, 16));
	}
}


VCL_BLOB
xyzzy_sweep_acl(VRT_CTX, VCL_ACL acl, VCL_IP ip0, VCL_IP ip1, VCL_INT step)
{
	struct acl_sweep asw[1];
	int i, j;
	struct vsb *vsb;
	char abuf[VTCP_ADDRBUFSIZE];
	char pbuf[VTCP_PORTBUFSIZE];
	unsigned char digest[VSHA256_DIGEST_LENGTH];
	struct VSHA256Context vsha[1];
	struct vrt_blob *b;
	ssize_t sz;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(acl);
	AN(ip0);
	AN(ip1);
	assert(step > 0);
	if (setup_sweep(ctx, asw, ip0, ip1, step))
		return (NULL);

	vsb = VSB_new_auto();
	AN(vsb);

	VSHA256_Init(vsha);
	for (j = 0; ; j++) {
		if ((j & 0x3f) == 0x00) {
			VTCP_name(asw->probe, abuf, sizeof abuf,
			    pbuf, sizeof pbuf);
			VSB_printf(vsb, "Sweep: %-15s", abuf);
		}
		i = VRT_acl_match(ctx, acl, asw->probe);
		assert(0 <= i && i <= 1);
		VSB_putc(vsb, "-X"[i]);
		if ((j & 0x3f) == 0x3f) {
			AZ(VSB_finish(vsb));
			VSLbs(ctx->vsl, SLT_Debug, TOSTRAND(VSB_data(vsb)));
			sz =VSB_len(vsb);
			assert (sz > 0);
			VSHA256_Update(vsha, VSB_data(vsb), sz);
			VSB_clear(vsb);
		}
		if (step_sweep(asw) > 0)
			break;
	}
	if (VSB_len(vsb)) {
		AZ(VSB_finish(vsb));
		VSLbs(ctx->vsl, SLT_Debug, TOSTRAND(VSB_data(vsb)));
		sz =VSB_len(vsb);
		assert (sz > 0);
		VSHA256_Update(vsha, VSB_data(vsb), sz);
		VSB_clear(vsb);
	}
	VSB_destroy(&vsb);

	VSHA256_Final(digest, vsha);
	b = WS_Alloc(ctx->ws, sizeof *b + sizeof digest);
	if (b != NULL) {
		memcpy(b + 1, digest, sizeof digest);
		b->blob = b + 1;
		b->len = sizeof digest;
	}
	cleanup_sweep(asw);
	return (b);
}

VCL_DURATION
xyzzy_time_acl(VRT_CTX, VCL_ACL acl, VCL_IP ip0, VCL_IP ip1,
    VCL_INT step, VCL_INT turnus)
{
	struct acl_sweep asw[1];
	vtim_mono t0, t1;
	vtim_dur d;
	VCL_INT cnt;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(acl);
	AN(ip0);
	AN(ip1);
	assert(step > 0);
	assert(turnus > 0);

	if (setup_sweep(ctx, asw, ip0, ip1, step))
		return (-1);
	do {
		(void)VRT_acl_match(ctx, acl, asw->probe);
	} while (step_sweep(asw) <= 0);
	asw->count = 0;
	t0 = VTIM_mono();
	for (cnt = 0; cnt < turnus; cnt++) {
		reset_sweep(asw);
		do {
			(void)VRT_acl_match(ctx, acl, asw->probe);
		} while (step_sweep(asw) <= 0);
	}
	t1 = VTIM_mono();
	cnt = asw->count;
	assert(cnt > 0);
	d = (t1 - t0) / cnt;
	VSLb(ctx->vsl, SLT_Debug,
	    "Timed ACL: %.9f -> %.9f = %.9f %.9f/round, %.9f/IP %ju IPs",
	    t0, t1, t1 - t0, (t1-t0) / turnus, d, (intmax_t)cnt);
	cleanup_sweep(asw);
	return (d);
}

struct xyzzy_debug_aclobj {
	unsigned			magic;
#define VMOD_DEBUG_ACLOBJ_MAGIC	0xac10ac10
	char *				vcl_name;
	VCL_ACL			acl;
};

VCL_VOID v_matchproto_(td_xyzzy_debug_aclobj__init)
xyzzy_aclobj__init(VRT_CTX, struct VPFX(debug_aclobj) **op,
    const char *vcl_name, VCL_ACL acl)
{
	struct VPFX(debug_aclobj) *o;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(op);
	AZ(*op);
	ALLOC_OBJ(o, VMOD_DEBUG_ACLOBJ_MAGIC);
	AN(o);
	REPLACE(o->vcl_name, vcl_name);
	o->acl = acl;
	*op = o;
}

VCL_VOID v_matchproto_(td_xyzzy_debug_aclobj__fini)
xyzzy_aclobj__fini(struct VPFX(debug_aclobj) **op)
{
	struct VPFX(debug_aclobj) *o;

	TAKE_OBJ_NOTNULL(o, op, VMOD_DEBUG_ACLOBJ_MAGIC);
	REPLACE(o->vcl_name, NULL);
	FREE_OBJ(o);
}

VCL_ACL v_matchproto_(td_xyzzy_debug_aclobj_get)
xyzzy_aclobj_get(VRT_CTX, struct VPFX(debug_aclobj) *o)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(o, VMOD_DEBUG_ACLOBJ_MAGIC);
	return (o->acl);
}
