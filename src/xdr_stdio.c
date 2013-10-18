/*
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <config.h>
#include <sys/cdefs.h>

/*
 * xdr_stdio.c, XDR implementation on standard i/o file.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * This set of routines implements a XDR on a stdio stream.
 * XDR_ENCODE serializes onto the stream, XDR_DECODE de-serializes
 * from the stream.
 */

#include "namespace.h"
#include <stdio.h>

#if !defined(_WIN32)
#include <arpa/inet.h>
#endif
#include <rpc/types.h>
#include <rpc/xdr.h>
#include "un-namespace.h"

static void xdrstdio_destroy(XDR *);
static bool xdrstdio_getlong(XDR *, long *);
static bool xdrstdio_putlong(XDR *, const long *);
static bool xdrstdio_getbytes(XDR *, char *, u_int);
static bool xdrstdio_putbytes(XDR *, const char *, u_int);
static u_int xdrstdio_getpos(XDR *);
static bool xdrstdio_setpos(XDR *, u_int);
static int32_t *xdrstdio_inline(XDR *, u_int);
static bool xdrstdio_noop(void);

typedef bool(*dummyfunc3) (XDR *, int, void *);
typedef bool(*dummy_getbufs) (XDR *, xdr_uio *, u_int, u_int);
typedef bool(*dummy_putbufs) (XDR *, xdr_uio *, u_int);

/*
 * Ops vector for stdio type XDR
 */
static const struct xdr_ops xdrstdio_ops = {
	xdrstdio_getlong,	/* deseraialize a long int */
	xdrstdio_putlong,	/* seraialize a long int */
	xdrstdio_getbytes,	/* deserialize counted bytes */
	xdrstdio_putbytes,	/* serialize counted bytes */
	xdrstdio_getpos,	/* get offset in the stream */
	xdrstdio_setpos,	/* set offset in the stream */
	xdrstdio_inline,	/* prime stream for inline macros */
	xdrstdio_destroy,	/* destroy stream */
	(dummyfunc3) xdrstdio_noop,	/* x_control */
	(dummy_getbufs) xdrstdio_noop,	/* x_getbufs */
	(dummy_putbufs) xdrstdio_noop	/* x_putbufs */
};

/*
 * Initialize a stdio xdr stream.
 * Sets the xdr stream handle xdrs for use on the stream file.
 * Operation flag is set to op.
 */
void
xdrstdio_create(XDR *xdrs, FILE *file, enum xdr_op op)
{

	xdrs->x_op = op;
	xdrs->x_ops = &xdrstdio_ops;
	xdrs->x_lib[0] = NULL;
	xdrs->x_lib[1] = NULL;
	xdrs->x_public = NULL;
	xdrs->x_private = file;
	xdrs->x_handy = 0;
	xdrs->x_base = 0;
}

/*
 * Destroy a stdio xdr stream.
 * Cleans up the xdr stream handle xdrs previously set up by xdrstdio_create.
 */
static void
xdrstdio_destroy(XDR *xdrs)
{
	(void)fflush((FILE *) xdrs->x_private);
	/* XXX: should we close the file ?? */
}

static bool
xdrstdio_getlong(XDR *xdrs, long *lp)
{
	if (fread(lp, sizeof(int32_t), 1, (FILE *) xdrs->x_private) != 1)
		return (FALSE);
	*lp = (long)ntohl((u_int32_t) *lp);
	return (TRUE);
}

static bool
xdrstdio_putlong(XDR *xdrs, const long *lp)
{
	long mycopy = (long)htonl((u_int32_t) *lp);

	if (fwrite(&mycopy, sizeof(int32_t), 1, (FILE *) xdrs->x_private) != 1)
		return (FALSE);
	return (TRUE);
}

static bool xdrstdio_getbytes(XDR *xdrs, char *addr, u_int len)
{
	if ((len != 0)
	    && (fread(addr, (size_t) len, 1, (FILE *) xdrs->x_private) != 1))
		return (FALSE);
	return (TRUE);
}

static bool
xdrstdio_putbytes(XDR *xdrs, const char *addr, u_int len)
{

	if ((len != 0)
	    && (fwrite(addr, (size_t) len, 1, (FILE *) xdrs->x_private) != 1))
		return (FALSE);
	return (TRUE);
}

static u_int
xdrstdio_getpos(XDR *xdrs)
{
	return ((u_int) ftell((FILE *) xdrs->x_private));
}

static bool
xdrstdio_setpos(XDR *xdrs, u_int pos)
{
	return ((fseek((FILE *) xdrs->x_private, (long)pos, 0) <
		 0) ? FALSE : TRUE);
}

/* ARGSUSED */
static int32_t *
xdrstdio_inline(XDR *xdrs, u_int len)
{
	/*
	 * Must do some work to implement this: must insure
	 * enough data in the underlying stdio buffer,
	 * that the buffer is aligned so that we can indirect through a
	 * long *, and stuff this pointer in xdrs->x_buf.  Doing
	 * a fread or fwrite to a scratch buffer would defeat
	 * most of the gains to be had here and require storage
	 * management on this buffer, so we don't do this.
	 */
	return (NULL);
}

static bool
xdrstdio_noop(void)
{
	return (FALSE);
}
