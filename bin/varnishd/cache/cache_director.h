/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2018 Varnish Software AS
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
 * This is the private implementation of directors.
 * You are not supposed to need anything here.
 *
 */

struct vdi_coollist;
struct vcldir_list;

struct vcldir {
	unsigned			magic;
#define VCLDIR_MAGIC			0xbf726c7d
	struct director			*dir;
	struct vcl			*vcl;
	const struct vdi_methods	*methods;
	// vcl->director list or vdi_coollist->director_list
	VTAILQ_ENTRY(vcldir)		list;
	const struct vdi_ahealth	*admin_health;
	double				health_changed;
	char				*cli_name;
	/* protected by global vdi_cool_mtx */
	unsigned			refcnt;
	struct vdi_coollist		*coollist;
};

#define VBE_AHEALTH_LIST					\
	VBE_AHEALTH(healthy,	HEALTHY,	1)		\
	VBE_AHEALTH(sick,	SICK,		0)		\
	VBE_AHEALTH(probe,	PROBE,		-1)		\
	VBE_AHEALTH(deleted,	DELETED,	0)

#define VBE_AHEALTH(l,u,h) extern const struct vdi_ahealth * const VDI_AH_##u;
VBE_AHEALTH_LIST
#undef VBE_AHEALTH

int VDI_Ref(VRT_CTX, struct vcldir *vdir);
void VDI_Dyn(VRT_CTX, struct vcldir *vdir);
int VDI_Unref(VRT_CTX, struct vcldir *vdir, struct vcldir_list *oldlist);
