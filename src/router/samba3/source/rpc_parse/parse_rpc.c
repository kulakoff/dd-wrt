/* 
 *  Unix SMB/CIFS implementation.
 *  RPC Pipe client / server routines
 *  Copyright (C) Andrew Tridgell              1992-1997,
 *  Copyright (C) Luke Kenneth Casson Leighton 1996-1997,
 *  Copyright (C) Paul Ashton                       1997.
 *  Copyright (C) Jeremy Allison                    1999.
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "includes.h"

#undef DBGC_CLASS
#define DBGC_CLASS DBGC_RPC_PARSE

/*******************************************************************
 Inits an RPC_HDR structure.
********************************************************************/

void init_rpc_hdr(RPC_HDR *hdr, enum RPC_PKT_TYPE pkt_type, uint8 flags,
				uint32 call_id, int data_len, int auth_len)
{
	hdr->major        = 5;               /* RPC version 5 */
	hdr->minor        = 0;               /* minor version 0 */
	hdr->pkt_type     = pkt_type;        /* RPC packet type */
	hdr->flags        = flags;           /* dce/rpc flags */
	hdr->pack_type[0] = 0x10;            /* little-endian data representation */
	hdr->pack_type[1] = 0;               /* packed data representation */
	hdr->pack_type[2] = 0;               /* packed data representation */
	hdr->pack_type[3] = 0;               /* packed data representation */
	hdr->frag_len     = data_len;        /* fragment length, fill in later */
	hdr->auth_len     = auth_len;        /* authentication length */
	hdr->call_id      = call_id;         /* call identifier - match incoming RPC */
}

/*******************************************************************
 Reads or writes an RPC_HDR structure.
********************************************************************/

bool smb_io_rpc_hdr(const char *desc,  RPC_HDR *rpc, prs_struct *ps, int depth)
{
	if (rpc == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_hdr");
	depth++;

	if(!prs_uint8 ("major     ", ps, depth, &rpc->major))
		return False;

	if(!prs_uint8 ("minor     ", ps, depth, &rpc->minor))
		return False;
	if(!prs_uint8 ("pkt_type  ", ps, depth, &rpc->pkt_type))
		return False;
	if(!prs_uint8 ("flags     ", ps, depth, &rpc->flags))
		return False;

	/* We always marshall in little endian format. */
	if (MARSHALLING(ps))
		rpc->pack_type[0] = 0x10;

	if(!prs_uint8("pack_type0", ps, depth, &rpc->pack_type[0]))
		return False;
	if(!prs_uint8("pack_type1", ps, depth, &rpc->pack_type[1]))
		return False;
	if(!prs_uint8("pack_type2", ps, depth, &rpc->pack_type[2]))
		return False;
	if(!prs_uint8("pack_type3", ps, depth, &rpc->pack_type[3]))
		return False;

	/*
	 * If reading and pack_type[0] == 0 then the data is in big-endian
	 * format. Set the flag in the prs_struct to specify reverse-endainness.
	 */

	if (UNMARSHALLING(ps) && rpc->pack_type[0] == 0) {
		DEBUG(10,("smb_io_rpc_hdr: PDU data format is big-endian. Setting flag.\n"));
		prs_set_endian_data(ps, RPC_BIG_ENDIAN);
	}

	if(!prs_uint16("frag_len  ", ps, depth, &rpc->frag_len))
		return False;
	if(!prs_uint16("auth_len  ", ps, depth, &rpc->auth_len))
		return False;
	if(!prs_uint32("call_id   ", ps, depth, &rpc->call_id))
		return False;
	return True;
}

/*******************************************************************
 Reads or writes an RPC_IFACE structure.
********************************************************************/

static bool smb_io_rpc_iface(const char *desc, RPC_IFACE *ifc, prs_struct *ps, int depth)
{
	if (ifc == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_iface");
	depth++;

	if (!prs_align(ps))
		return False;

	if (!smb_io_uuid(  "uuid", &ifc->uuid, ps, depth))
		return False;

	if(!prs_uint32 ("version", ps, depth, &ifc->if_version))
		return False;

	return True;
}

/*******************************************************************
 Inits an RPC_ADDR_STR structure.
********************************************************************/

static void init_rpc_addr_str(RPC_ADDR_STR *str, const char *name)
{
	str->len = strlen(name) + 1;
	fstrcpy(str->str, name);
}

/*******************************************************************
 Reads or writes an RPC_ADDR_STR structure.
********************************************************************/

static bool smb_io_rpc_addr_str(const char *desc,  RPC_ADDR_STR *str, prs_struct *ps, int depth)
{
	if (str == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_addr_str");
	depth++;
	if(!prs_align(ps))
		return False;

	if(!prs_uint16 (      "len", ps, depth, &str->len))
		return False;
	if(!prs_uint8s (True, "str", ps, depth, (uchar*)str->str, MIN(str->len, sizeof(str->str)) ))
		return False;
	return True;
}

/*******************************************************************
 Inits an RPC_HDR_BBA structure.
********************************************************************/

static void init_rpc_hdr_bba(RPC_HDR_BBA *bba, uint16 max_tsize, uint16 max_rsize, uint32 assoc_gid)
{
	bba->max_tsize = max_tsize; /* maximum transmission fragment size (0x1630) */
	bba->max_rsize = max_rsize; /* max receive fragment size (0x1630) */   
	bba->assoc_gid = assoc_gid; /* associated group id (0x0) */ 
}

/*******************************************************************
 Reads or writes an RPC_HDR_BBA structure.
********************************************************************/

static bool smb_io_rpc_hdr_bba(const char *desc,  RPC_HDR_BBA *rpc, prs_struct *ps, int depth)
{
	if (rpc == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_hdr_bba");
	depth++;

	if(!prs_uint16("max_tsize", ps, depth, &rpc->max_tsize))
		return False;
	if(!prs_uint16("max_rsize", ps, depth, &rpc->max_rsize))
		return False;
	if(!prs_uint32("assoc_gid", ps, depth, &rpc->assoc_gid))
		return False;
	return True;
}

/*******************************************************************
 Inits an RPC_CONTEXT structure.
 Note the transfer pointer must remain valid until this is marshalled.
********************************************************************/

void init_rpc_context(RPC_CONTEXT *rpc_ctx, uint16 context_id,
		      const RPC_IFACE *abstract, const RPC_IFACE *transfer)
{
	rpc_ctx->context_id   = context_id   ; /* presentation context identifier (0x0) */
	rpc_ctx->num_transfer_syntaxes = 1 ; /* the number of syntaxes (has always been 1?)(0x1) */

	/* num and vers. of interface client is using */
	rpc_ctx->abstract = *abstract;

	/* vers. of interface to use for replies */
	rpc_ctx->transfer = CONST_DISCARD(RPC_IFACE *, transfer);
}

/*******************************************************************
 Inits an RPC_HDR_RB structure.
 Note the context pointer must remain valid until this is marshalled.
********************************************************************/

void init_rpc_hdr_rb(RPC_HDR_RB *rpc, 
				uint16 max_tsize, uint16 max_rsize, uint32 assoc_gid,
				RPC_CONTEXT *context)
{
	init_rpc_hdr_bba(&rpc->bba, max_tsize, max_rsize, assoc_gid);

	rpc->num_contexts = 1;
	rpc->rpc_context = context;
}

/*******************************************************************
 Reads or writes an RPC_CONTEXT structure.
********************************************************************/

bool smb_io_rpc_context(const char *desc, RPC_CONTEXT *rpc_ctx, prs_struct *ps, int depth)
{
	int i;

	if (rpc_ctx == NULL)
		return False;

	if(!prs_align(ps))
		return False;
	if(!prs_uint16("context_id  ", ps, depth, &rpc_ctx->context_id ))
		return False;
	if(!prs_uint8 ("num_transfer_syntaxes", ps, depth, &rpc_ctx->num_transfer_syntaxes))
		return False;

	/* num_transfer_syntaxes must not be zero. */
	if (rpc_ctx->num_transfer_syntaxes == 0)
		return False;

	if(!smb_io_rpc_iface("", &rpc_ctx->abstract, ps, depth))
		return False;

	if (UNMARSHALLING(ps)) {
		if (!(rpc_ctx->transfer = PRS_ALLOC_MEM(ps, RPC_IFACE, rpc_ctx->num_transfer_syntaxes))) {
			return False;
		}
	}

	for (i = 0; i < rpc_ctx->num_transfer_syntaxes; i++ ) {
		if (!smb_io_rpc_iface("", &rpc_ctx->transfer[i], ps, depth))
			return False;
	}
	return True;
} 

/*******************************************************************
 Reads or writes an RPC_HDR_RB structure.
********************************************************************/

bool smb_io_rpc_hdr_rb(const char *desc, RPC_HDR_RB *rpc, prs_struct *ps, int depth)
{
	int i;
	
	if (rpc == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_hdr_rb");
	depth++;

	if(!smb_io_rpc_hdr_bba("", &rpc->bba, ps, depth))
		return False;

	if(!prs_uint8("num_contexts", ps, depth, &rpc->num_contexts))
		return False;

	/* 3 pad bytes following - will be mopped up by the prs_align in smb_io_rpc_context(). */

	/* num_contexts must not be zero. */
	if (rpc->num_contexts == 0)
		return False;

	if (UNMARSHALLING(ps)) {
		if (!(rpc->rpc_context = PRS_ALLOC_MEM(ps, RPC_CONTEXT, rpc->num_contexts))) {
			return False;
		}
	}

	for (i = 0; i < rpc->num_contexts; i++ ) {
		if (!smb_io_rpc_context("", &rpc->rpc_context[i], ps, depth))
			return False;
	}

	return True;
}

/*******************************************************************
 Inits an RPC_RESULTS structure.

 lkclXXXX only one reason at the moment!
********************************************************************/

static void init_rpc_results(RPC_RESULTS *res, 
				uint8 num_results, uint16 result, uint16 reason)
{
	res->num_results = num_results; /* the number of results (0x01) */
	res->result      = result     ;  /* result (0x00 = accept) */
	res->reason      = reason     ;  /* reason (0x00 = no reason specified) */
}

/*******************************************************************
 Reads or writes an RPC_RESULTS structure.

 lkclXXXX only one reason at the moment!
********************************************************************/

static bool smb_io_rpc_results(const char *desc, RPC_RESULTS *res, prs_struct *ps, int depth)
{
	if (res == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_results");
	depth++;

	if(!prs_align(ps))
		return False;
	
	if(!prs_uint8 ("num_results", ps, depth, &res->num_results))	
		return False;

	if(!prs_align(ps))
		return False;
	
	if(!prs_uint16("result     ", ps, depth, &res->result))
		return False;
	if(!prs_uint16("reason     ", ps, depth, &res->reason))
		return False;
	return True;
}

/*******************************************************************
 Init an RPC_HDR_BA structure.

 lkclXXXX only one reason at the moment!

********************************************************************/

void init_rpc_hdr_ba(RPC_HDR_BA *rpc, 
				uint16 max_tsize, uint16 max_rsize, uint32 assoc_gid,
				const char *pipe_addr,
				uint8 num_results, uint16 result, uint16 reason,
				RPC_IFACE *transfer)
{
	init_rpc_hdr_bba (&rpc->bba, max_tsize, max_rsize, assoc_gid);
	init_rpc_addr_str(&rpc->addr, pipe_addr);
	init_rpc_results (&rpc->res, num_results, result, reason);

	/* the transfer syntax from the request */
	memcpy(&rpc->transfer, transfer, sizeof(rpc->transfer));
}

/*******************************************************************
 Reads or writes an RPC_HDR_BA structure.
********************************************************************/

bool smb_io_rpc_hdr_ba(const char *desc, RPC_HDR_BA *rpc, prs_struct *ps, int depth)
{
	if (rpc == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_hdr_ba");
	depth++;

	if(!smb_io_rpc_hdr_bba("", &rpc->bba, ps, depth))
		return False;
	if(!smb_io_rpc_addr_str("", &rpc->addr, ps, depth))
		return False;
	if(!smb_io_rpc_results("", &rpc->res, ps, depth))
		return False;
	if(!smb_io_rpc_iface("", &rpc->transfer, ps, depth))
		return False;
	return True;
}

/*******************************************************************
 Init an RPC_HDR_REQ structure.
********************************************************************/

void init_rpc_hdr_req(RPC_HDR_REQ *hdr, uint32 alloc_hint, uint16 opnum)
{
	hdr->alloc_hint   = alloc_hint; /* allocation hint */
	hdr->context_id   = 0;         /* presentation context identifier */
	hdr->opnum        = opnum;     /* opnum */
}

/*******************************************************************
 Reads or writes an RPC_HDR_REQ structure.
********************************************************************/

bool smb_io_rpc_hdr_req(const char *desc, RPC_HDR_REQ *rpc, prs_struct *ps, int depth)
{
	if (rpc == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_hdr_req");
	depth++;

	if(!prs_uint32("alloc_hint", ps, depth, &rpc->alloc_hint))
		return False;
	if(!prs_uint16("context_id", ps, depth, &rpc->context_id))
		return False;
	if(!prs_uint16("opnum     ", ps, depth, &rpc->opnum))
		return False;
	return True;
}

/*******************************************************************
 Reads or writes an RPC_HDR_RESP structure.
********************************************************************/

bool smb_io_rpc_hdr_resp(const char *desc, RPC_HDR_RESP *rpc, prs_struct *ps, int depth)
{
	if (rpc == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_hdr_resp");
	depth++;

	if(!prs_uint32("alloc_hint", ps, depth, &rpc->alloc_hint))
		return False;
	if(!prs_uint16("context_id", ps, depth, &rpc->context_id))
		return False;
	if(!prs_uint8 ("cancel_ct ", ps, depth, &rpc->cancel_count))
		return False;
	if(!prs_uint8 ("reserved  ", ps, depth, &rpc->reserved))
		return False;
	return True;
}

/*******************************************************************
 Reads or writes an RPC_HDR_FAULT structure.
********************************************************************/

bool smb_io_rpc_hdr_fault(const char *desc, RPC_HDR_FAULT *rpc, prs_struct *ps, int depth)
{
	if (rpc == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_hdr_fault");
	depth++;

	if(!prs_dcerpc_status("status  ", ps, depth, &rpc->status))
		return False;
	if(!prs_uint32("reserved", ps, depth, &rpc->reserved))
		return False;

    return True;
}

/*******************************************************************
 Inits an RPC_HDR_AUTH structure.
********************************************************************/

void init_rpc_hdr_auth(RPC_HDR_AUTH *rai,
				uint8 auth_type, uint8 auth_level,
				uint8 auth_pad_len,
				uint32 auth_context_id)
{
	rai->auth_type     = auth_type;
	rai->auth_level    = auth_level;
	rai->auth_pad_len  = auth_pad_len;
	rai->auth_reserved = 0;
	rai->auth_context_id = auth_context_id;
}

/*******************************************************************
 Reads or writes an RPC_HDR_AUTH structure.
********************************************************************/

bool smb_io_rpc_hdr_auth(const char *desc, RPC_HDR_AUTH *rai, prs_struct *ps, int depth)
{
	if (rai == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_hdr_auth");
	depth++;

	if(!prs_align(ps))
		return False;

	if(!prs_uint8 ("auth_type    ", ps, depth, &rai->auth_type))
		return False;
	if(!prs_uint8 ("auth_level   ", ps, depth, &rai->auth_level))
		return False;
	if(!prs_uint8 ("auth_pad_len ", ps, depth, &rai->auth_pad_len))
		return False;
	if(!prs_uint8 ("auth_reserved", ps, depth, &rai->auth_reserved))
		return False;
	if(!prs_uint32("auth_context_id", ps, depth, &rai->auth_context_id))
		return False;

	return True;
}

/*******************************************************************
 Checks an RPC_AUTH_VERIFIER structure.
********************************************************************/

bool rpc_auth_verifier_chk(RPC_AUTH_VERIFIER *rav,
				const char *signature, uint32 msg_type)
{
	return (strequal(rav->signature, signature) && rav->msg_type == msg_type);
}

/*******************************************************************
 Inits an RPC_AUTH_VERIFIER structure.
********************************************************************/

void init_rpc_auth_verifier(RPC_AUTH_VERIFIER *rav,
				const char *signature, uint32 msg_type)
{
	fstrcpy(rav->signature, signature); /* "NTLMSSP" */
	rav->msg_type = msg_type; /* NTLMSSP_MESSAGE_TYPE */
}

/*******************************************************************
 Reads or writes an RPC_AUTH_VERIFIER structure.
********************************************************************/

bool smb_io_rpc_auth_verifier(const char *desc, RPC_AUTH_VERIFIER *rav, prs_struct *ps, int depth)
{
	if (rav == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_auth_verifier");
	depth++;

	/* "NTLMSSP" */
	if(!prs_string("signature", ps, depth, rav->signature,
			sizeof(rav->signature)))
		return False;
	if(!prs_uint32("msg_type ", ps, depth, &rav->msg_type)) /* NTLMSSP_MESSAGE_TYPE */
		return False;

	return True;
}

/*******************************************************************
 This parses an RPC_AUTH_VERIFIER for schannel. I think
********************************************************************/

bool smb_io_rpc_schannel_verifier(const char *desc, RPC_AUTH_VERIFIER *rav, prs_struct *ps, int depth)
{
	if (rav == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_schannel_verifier");
	depth++;

	if(!prs_string("signature", ps, depth, rav->signature, sizeof(rav->signature)))
		return False;
	if(!prs_uint32("msg_type ", ps, depth, &rav->msg_type))
		return False;

	return True;
}

/*******************************************************************
creates an RPC_AUTH_SCHANNEL_NEG structure.
********************************************************************/

void init_rpc_auth_schannel_neg(RPC_AUTH_SCHANNEL_NEG *neg,
			      const char *domain, const char *myname)
{
	neg->type1 = 0;
	neg->type2 = 0x3;
	fstrcpy(neg->domain, domain);
	fstrcpy(neg->myname, myname);
}

/*******************************************************************
 Reads or writes an RPC_AUTH_SCHANNEL_NEG structure.
********************************************************************/

bool smb_io_rpc_auth_schannel_neg(const char *desc, RPC_AUTH_SCHANNEL_NEG *neg,
				prs_struct *ps, int depth)
{
	if (neg == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_auth_schannel_neg");
	depth++;

	if(!prs_align(ps))
		return False;

	if(!prs_uint32("type1", ps, depth, &neg->type1))
		return False;
	if(!prs_uint32("type2", ps, depth, &neg->type2))
		return False;
	if(!prs_string("domain  ", ps, depth, neg->domain, sizeof(neg->domain)))
		return False;
	if(!prs_string("myname  ", ps, depth, neg->myname, sizeof(neg->myname)))
		return False;

	return True;
}

/*******************************************************************
reads or writes an RPC_AUTH_SCHANNEL_CHK structure.
********************************************************************/

bool smb_io_rpc_auth_schannel_chk(const char *desc, int auth_len, 
                                RPC_AUTH_SCHANNEL_CHK * chk,
				prs_struct *ps, int depth)
{
	if (chk == NULL)
		return False;

	prs_debug(ps, depth, desc, "smb_io_rpc_auth_schannel_chk");
	depth++;

	if ( !prs_uint8s(False, "sig  ", ps, depth, chk->sig, sizeof(chk->sig)) )
		return False;
		
	if ( !prs_uint8s(False, "seq_num", ps, depth, chk->seq_num, sizeof(chk->seq_num)) )
		return False;
		
	if ( !prs_uint8s(False, "packet_digest", ps, depth, chk->packet_digest, sizeof(chk->packet_digest)) )
		return False;
	
	if ( auth_len == RPC_AUTH_SCHANNEL_SIGN_OR_SEAL_CHK_LEN ) {
		if ( !prs_uint8s(False, "confounder", ps, depth, chk->confounder, sizeof(chk->confounder)) )
			return False;
	}

	return True;
}

const struct ndr_syntax_id syntax_spoolss = {
	{
		0x12345678, 0x1234, 0xabcd,
		{ 0xef, 0x00 },
		{ 0x01, 0x23,
		  0x45, 0x67, 0x89, 0xab }
	}, 0x01
};

