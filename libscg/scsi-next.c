/* @(#)scsi-next.c	1.20 00/07/01 Copyright 1997 J. Schilling */
#ifndef lint
static	char __sccsid[] =
	"@(#)scsi-next.c	1.20 00/07/01 Copyright 1997 J. Schilling";
#endif
/*
 *	Interface for the NeXT Step generic SCSI implementation.
 *
 *	This is a hack, that tries to emulate the functionality
 *	of the scg driver.
 *
 *	Warning: you may change this source, but if you do that
 *	you need to change the _scg_version and _scg_auth* string below.
 *	You may not return "schily" for an SCG_AUTHOR request anymore.
 *	Choose your name instead of "schily" and make clear that the version
 *	string is related to a modified source.
 *
 *	Copyright (c) 1997 J. Schilling
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <bsd/dev/scsireg.h>

/*
 *	Warning: you may change this source, but if you do that
 *	you need to change the _scg_version and _scg_auth* string below.
 *	You may not return "schily" for an SCG_AUTHOR request anymore.
 *	Choose your name instead of "schily" and make clear that the version
 *	string is related to a modified source.
 */
LOCAL	char	_scg_trans_version[] = "scsi-next.c-1.20";	/* The version for this transport*/

#define	MAX_SCG		16	/* Max # of SCSI controllers */
#define	MAX_TGT		16
#define	MAX_LUN		8

struct scg_local {
	short	scgfiles[MAX_SCG][MAX_TGT][MAX_LUN];
	int	scgfile;
	int	max_scsibus;
	int	cur_scsibus;
	int	cur_target;
	int	cur_lun;
};
#define scglocal(p)	((struct scg_local *)((p)->local)) 

/*#define	MAX_DMA_NEXT	(32*1024)*/
#define	MAX_DMA_NEXT	(64*1024)	/* Check if this is not too big */


LOCAL	BOOL	scsi_setup	__PR((SCSI *scgp, int busno, int tgt, int tlun,
								BOOL ex));

/*
 * Return version information for the low level SCSI transport code.
 * This has been introduced to make it easier to trace down problems
 * in applications.
 */
EXPORT char *
scg__version(scgp, what)
	SCSI	*scgp;
	int	what;
{
	if (scgp != (SCSI *)0) {
		switch (what) {

		case SCG_VERSION:
			return (_scg_trans_version);
		/*
		 * If you changed this source, you are not allowed to
		 * return "schily" for the SCG_AUTHOR request.
		 */
		case SCG_AUTHOR:
			return (_scg_auth_schily);
		case SCG_SCCS_ID:
			return (__sccsid);
		}
	}
	return ((char *)0);
}

EXPORT int
scsi_open(scgp, device, busno, tgt, tlun)
	SCSI	*scgp;
	char	*device;
	int	busno;
	int	tgt;
	int	tlun;
{
	register int	f;
	register int	i;
	char		devname[64];

	if (busno >= MAX_SCG || tgt >= MAX_TGT || tlun >= MAX_LUN) {
		errno = EINVAL;
		if (scgp->errstr)
			js_snprintf(scgp->errstr, SCSI_ERRSTR_SIZE,
				"Illegal value for busno, target or lun '%d,%d,%d'",
				busno, tgt, tlun);
		return (-1);
	}

	if ((device != NULL && *device != '\0') || (busno == -2 && tgt == -2)) {
		errno = EINVAL;
		if (scgp->errstr)
			js_snprintf(scgp->errstr, SCSI_ERRSTR_SIZE,
				"Open by 'devname' not supported on this OS");
		return (-1);
	}

	if (scgp->local == NULL) {
		scgp->local = malloc(sizeof(struct scg_local));
		if (scgp->local == NULL)
			return (0);

		scglocal(scgp)->scgfile		= -1;
		scglocal(scgp)->max_scsibus	= -1;
		scglocal(scgp)->cur_scsibus	= -1;
		scglocal(scgp)->cur_target	= -1;
		scglocal(scgp)->cur_lun		= -1;
	}

	for (i=0; i < 4; i++) {
		sprintf(devname, "/dev/sg%d", i);
		f = open(devname, O_RDWR);
		if (scgp->debug)
			errmsg("open(devname: '%s') : %d\n", devname, f);
		if (f < 0)
			continue;
		scglocal(scgp)->scgfile = f;
		break;

	}
	if (f >= 0) {
		if (scglocal(scgp)->max_scsibus < 0) {
			for (i=0; i < MAX_SCG; i++) {
				if (!scsi_havebus(scgp, i))
					break;
			}
			scglocal(scgp)->max_scsibus = i;
		}
		if (scgp->debug)
			printf("maxbus: %d\n", scglocal(scgp)->max_scsibus);
		if (scglocal(scgp)->max_scsibus <= 0) {
			scglocal(scgp)->max_scsibus = 1;
			scglocal(scgp)->cur_scsibus = 0;
		}

       	        ioctl(f, SGIOCENAS);
		if (busno > 0 && tgt > 0 && tlun > 0)
			scsi_setup(scgp, busno, tgt, tlun, TRUE);
		return(1);
	}
	if (scgp->errstr)
		js_snprintf(scgp->errstr, SCSI_ERRSTR_SIZE,
			"Cannot open '/dev/sg*'");
	return (0);
}

EXPORT int
scsi_close(scgp)
	SCSI	*scgp;
{
	if (scgp->local == NULL)
		return (-1);

	if (scglocal(scgp)->scgfile >= 0)
		close(scglocal(scgp)->scgfile);
	scglocal(scgp)->scgfile = -1;
	return (0);
}

LOCAL BOOL
scsi_setup(scgp, busno, tgt, tlun, ex)
	SCSI	*scgp;
	int	busno;
	int	tgt;
	int	tlun;
	BOOL	ex;
{
	scsi_adr_t sadr;

	sadr.sa_target = tgt;
	sadr.sa_lun = tlun;

	if (scgp->debug)
		printf("scsi_setup curbus %d -> %d\n", scglocal(scgp)->cur_scsibus, busno);

	if (scgp->debug && ((scglocal(scgp)->cur_scsibus < 0 || scglocal(scgp)->cur_scsibus != busno)))
		printf("setting SCSI bus to: %d\n", busno); 
	if ((scglocal(scgp)->cur_scsibus < 0 || scglocal(scgp)->cur_scsibus != busno) &&
				ioctl(scglocal(scgp)->scgfile, SGIOCCNTR, &busno) < 0) {

		scglocal(scgp)->cur_scsibus = -1;	/* Driver is in undefined state */
		if (ex)
/*			comerr("Cannot set SCSI bus\n");*/
			errmsg("Cannot set SCSI bus\n");
		return (FALSE);
	}
	scglocal(scgp)->cur_scsibus	= busno;

	if (scgp->debug)
		printf("setting target/lun to: %d/%d\n", tgt, tlun);
	if (ioctl(scglocal(scgp)->scgfile, SGIOCSTL, &sadr) < 0) {
		if (ex)
			comerr("Cannot set SCSI address\n");
		return (FALSE);
	}
	scglocal(scgp)->cur_scsibus	= busno;
	scglocal(scgp)->cur_target	= tgt;
	scglocal(scgp)->cur_lun		= tlun;
	return (TRUE);
}

LOCAL long
scsi_maxdma(scgp, amt)
	SCSI	*scgp;
	long	amt;
{
	long maxdma = MAX_DMA_NEXT;
#ifdef	SGIOCMAXDMA
	int  m;

	if (ioctl(scglocal(scgp)->scgfile, SGIOCMAXDMA, &m) >= 0)
		maxdma = m;
		if (scgp->debug)
			printf("maxdma: %d\n", maxdma);
#endif
	return (maxdma);
}
#ifdef	XXX
#define SGIOCENAS	_IO(  's', 2)		 	/* enable autosense */
#define SGIOCDAS	_IO(  's', 3)			/* disable autosense */
#define SGIOCRST	_IO(  's', 4)			/* reset SCSI bus */
#define SGIOCCNTR       _IOW( 's', 6, int)              /* select controller */
#define SGIOCGAS	_IOR( 's', 7, int)		/* get autosense */
#define SGIOCMAXDMA	_IOR( 's', 8, int)		/* max DMA size */
#define SGIOCNUMTARGS	_IOR( 's', 9, int)		/* # of targets/bus */
#endif

EXPORT void *
scsi_getbuf(scgp, amt)
	SCSI	*scgp;
	long	amt;
{
	if (amt <= 0 || amt > scsi_bufsize(scgp, amt))
		return ((void *)0);
	if (scgp->debug)
		printf("scsi_getbuf: %ld bytes\n", amt);
	scgp->bufbase = valloc((size_t)(amt));
	return (scgp->bufbase);
}

EXPORT void
scsi_freebuf(scgp)
	SCSI	*scgp;
{
	if (scgp->bufbase)
		free(scgp->bufbase);
	scgp->bufbase = NULL;
}

EXPORT
BOOL scsi_havebus(scgp, busno)
	SCSI	*scgp;
	int	busno;
{
	if (busno < 0 || busno >= MAX_SCG)
		return (FALSE);
	if (scglocal(scgp)->max_scsibus > 0 && busno >= scglocal(scgp)->max_scsibus)
		return (FALSE);

	if (scgp->local == NULL)
		return (FALSE);

	return (scsi_setup(scgp, busno, 0, 0, FALSE));
}

EXPORT
int scsi_fileno(scgp, busno, tgt, tlun)
	SCSI	*scgp;
	int	busno;
	int	tgt;
	int	tlun;
{
	if (busno < 0 || busno >= MAX_SCG ||
	    tgt < 0 || tgt >= MAX_TGT ||
	    tlun < 0 || tlun >= MAX_LUN)
		return (-1);
	if (scglocal(scgp)->max_scsibus > 0 && busno >= scglocal(scgp)->max_scsibus)
		return (-1);

	if (scgp->local == NULL)
		return (-1);

	if ((busno != scglocal(scgp)->cur_scsibus) || (tgt != scglocal(scgp)->cur_target) || (tlun != scglocal(scgp)->cur_lun)) {
		if (!scsi_setup(scgp, busno, tgt, tlun, FALSE))
			return (-1);
	}
	return (scglocal(scgp)->scgfile);
}

EXPORT int
scsi_initiator_id(scgp)
	SCSI	*scgp;
{
	return (-1);
}

EXPORT
int scsi_isatapi(scgp)
	SCSI	*scgp;

{
	return (FALSE);
}

EXPORT
int scsireset(scgp)
	SCSI	*scgp;
{
	int	f = scsi_fileno(scgp, scgp->scsibus, scgp->target, scgp->lun);

	return (ioctl(f, SGIOCRST, 0));
}

LOCAL int
scsi_send(scgp, f, sp)
	SCSI		*scgp;
	int		f;
	struct scg_cmd	*sp;
{
	struct scsi_req	req;
	register long	*lp1;
	register long	*lp2;
	int		ret = 0;

	if (f < 0 || (sp->cdb_len > sizeof(req.sr_cdb))) {
		sp->error = SCG_FATAL;
		sp->ux_errno = EIO;
		return (0);
	}
	fillbytes(&req, sizeof(req), '\0');
	movebytes(sp->cdb.cmd_cdb, &req.sr_cdb, sp->cdb_len);
	if (sp->size) {
		req.sr_dma_dir = SR_DMA_WR;
		if (sp->flags & SCG_RECV_DATA)
			req.sr_dma_dir = SR_DMA_RD;
	}
	req.sr_addr = sp->addr;
	req.sr_dma_max = sp->size;
	req.sr_ioto = sp->timeout;
	if (ioctl(f, SGIOCREQ, (void *)&req) < 0) {
		ret  = -1;
		sp->ux_errno = geterrno();
		if (sp->ux_errno != ENOTTY)
			ret = 0;
	} else {
		sp->ux_errno = 0;
	}
	if (scgp->debug) {
		printf("dma_dir:     %X\n", req.sr_dma_dir);
		printf("dma_addr:    %X\n", req.sr_addr);
		printf("io_time:     %d\n", req.sr_ioto);
		printf("io_status:   %d\n", req.sr_io_status);
		printf("scsi_status: %X\n", req.sr_scsi_status);
		printf("dma_xfer:    %d\n", req.sr_dma_xfr);
	}
	sp->u_scb.cmd_scb[0] = req.sr_scsi_status;
	sp->sense_count = sizeof(esense_reply_t);
	if (sp->sense_count > sp->sense_len)
		sp->sense_count = sp->sense_len;
	if (sp->sense_count > SCG_MAX_SENSE)
		sp->sense_count = SCG_MAX_SENSE;
	if (sp->sense_count < 0)
		sp->sense_count = 0;
	movebytes(&req.sr_esense, sp->u_sense.cmd_sense, sp->sense_count);
	sp->resid = sp->size - req.sr_dma_xfr;

	switch (req.sr_io_status) {

	case SR_IOST_GOOD:	sp->error = SCG_NO_ERROR;	break;

	case SR_IOST_CHKSNV:	sp->sense_count = 0;
	case SR_IOST_CHKSV:	sp->error = SCG_RETRYABLE;
				break;
	
	case SR_IOST_SELTO:
	case SR_IOST_DMAOR:
				sp->error = SCG_FATAL;		break;

	case SR_IOST_IOTO:	sp->error = SCG_TIMEOUT;	break;

	case SR_IOST_PERM:
	case SR_IOST_NOPEN:
				sp->error = SCG_FATAL;
				ret = (-1);
				break;

	default:		sp->error = SCG_RETRYABLE;	break;

	}
	return (ret);
}