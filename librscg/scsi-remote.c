#define	USE_REMOTE
#ifdef	USE_REMOTE
/* @(#)scsi-remote.c	1.3 00/09/10 Copyright 1990,2000 J. Schilling */
#ifndef lint
static	char __sccsid[] =
	"@(#)scsi-remote.c	1.3 00/09/10 Copyright 1990,2000 J. Schilling";
#endif
/*
 *	Remote SCSI user level command transport routines
 *
 *	Warning: you may change this source, but if you do that
 *	you need to change the _scg_version and _scg_auth* string below.
 *	You may not return "schily" for an SCG_AUTHOR request anymore.
 *	Choose your name instead of "schily" and make clear that the version
 *	string is related to a modified source.
 *
 *	Copyright (c) 1990,2000 J. Schilling
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

#include <mconfig.h>
#include <stdio.h>
#include <sys/types.h>
#include <fctldefs.h>
#include <sys/socket.h>
#include <errno.h>
#include <signal.h>
#include <netdb.h>
#include <pwd.h>
#include <standard.h>
#include <stdxlib.h>
#include <unixstd.h>
#include <strdefs.h>
#include <schily.h>

#include <scg/scgcmd.h>
#include <scg/scsitransp.h>

#if	defined(SIGDEFER) || defined(SVR4)
#define	signal	sigset
#endif

#define	CMD_SIZE	80

#define	MAX_SCG		16	/* Max # of SCSI controllers */
#define	MAX_TGT		16
#define	MAX_LUN		8

/*extern	BOOL	debug;*/
LOCAL	BOOL	debug = 1;

LOCAL	char	_scg_trans_version[] = "remote-1.3";	/* The version for remote SCSI	*/
LOCAL	char	_scg_auth_schily[]	= "schily";	/* The author for this module	*/

LOCAL	int	scgo_rsend		__PR((SCSI *scgp));
LOCAL	char *	scgo_rversion		__PR((SCSI *scgp, int what));
LOCAL	int	scgo_ropen		__PR((SCSI *scgp, char *device));
LOCAL	int	scgo_rclose		__PR((SCSI *scgp));
LOCAL	long	scgo_rmaxdma		__PR((SCSI *scgp, long amt));
LOCAL	void *	scgo_rgetbuf		__PR((SCSI *scgp, long amt));
LOCAL	void	scgo_rfreebuf		__PR((SCSI *scgp));
LOCAL	BOOL	scgo_rhavebus		__PR((SCSI *scgp, int busno));
LOCAL	int	scgo_rfileno		__PR((SCSI *scgp, int busno, int tgt, int tlun));
LOCAL	int	scgo_rinitiator_id	__PR((SCSI *scgp));
LOCAL	int	scgo_risatapi		__PR((SCSI *scgp));
LOCAL	int	scgo_rreset		__PR((SCSI *scgp, int what));

LOCAL	void	rscsiabrt		__PR((int sig));
LOCAL	int	rscsigetconn		__PR((SCSI *scgp, char* host));
LOCAL	char	*rscsiversion		__PR((SCSI *scgp, int fd, int what));
LOCAL	int	rscsiopen		__PR((SCSI *scgp, int fd, char* fname));
LOCAL	int	rscsiclose		__PR((SCSI *scgp, int fd));
LOCAL	int	rscsimaxdma		__PR((SCSI *scgp, int fd, long amt));
LOCAL	int	rscsigetbuf		__PR((SCSI *scgp, int fd, long amt));
LOCAL	int	rscsifreebuf		__PR((SCSI *scgp, int fd));
LOCAL	int	rscsihavebus		__PR((SCSI *scgp, int fd, int bus));
LOCAL	int	rscsifileno		__PR((SCSI *scgp, int fd, int busno, int tgt, int tlun));
LOCAL	int	rscsiinitiator_id	__PR((SCSI *scgp, int fd));
LOCAL	int	rscsiisatapi		__PR((SCSI *scgp, int fd));
LOCAL	int	rscsireset		__PR((SCSI *scgp, int fd, int what));
LOCAL	int	rscsiscmd		__PR((SCSI *scgp, int fd, struct scg_cmd *sp));
LOCAL	int	rscsireadbuf		__PR((SCSI *scgp, int fd, char *buf, int count));
LOCAL	void	rscsivoidarg		__PR((SCSI *scgp, int fd, int count));
LOCAL	int	rscsicmd		__PR((SCSI *scgp, int fd, char* name, char* cbuf));
LOCAL	void	rscsisendcmd		__PR((SCSI *scgp, int fd, char* name, char* cbuf));
LOCAL	int	rscsigetline		__PR((SCSI *scgp, int fd, char* line, int count));
LOCAL	int	rscsireadnum		__PR((SCSI *scgp, int fd));
LOCAL	int	rscsigetstatus		__PR((SCSI *scgp, int fd, char* name));
LOCAL	int	rscsiaborted		__PR((SCSI *scgp, int fd));

/*--------------------------------------------------------------------------*/

struct scg_local {
	int	remfd;
	BOOL	isopen;
	int	rsize;
	int	wsize;
	char	*v_version;
	char	*v_author;
	char	*v_sccs_id;
};

#define scglocal(p)	((struct scg_local *)((p)->local))

scg_ops_t remote_ops = {
	scgo_rsend,		/* "S" end	*/
	scgo_rversion,		/* "V" ersion	*/
	scgo_ropen,		/* "O" pen	*/
	scgo_rclose,		/* "C" lose	*/
	scgo_rmaxdma,		/* "D" MA	*/
	scgo_rgetbuf,		/* "M" alloc	*/
	scgo_rfreebuf,		/* "F" free	*/
	scgo_rhavebus,		/* "B" us	*/
	scgo_rfileno,		/* "T" arget	*/
	scgo_rinitiator_id,	/* "I" nitiator	*/
	scgo_risatapi,		/* "A" tapi	*/
	scgo_rreset,		/* "R" eset	*/
};

/*
 * Return our ops ptr.
 */
scg_ops_t *
scg_remote()
{
	return (&remote_ops);
}

/*
 * Return version information for the low level SCSI transport code.
 * This has been introduced to make it easier to trace down problems
 * in applications.
 */
LOCAL char *
scgo_rversion(scgp, what)
	SCSI	*scgp;
	int	what;
{
	int	f;

	if (scgp->local == NULL)
		return ((char *)0);

	f = scglocal(scgp)->remfd;
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

		case SCG_RVERSION:
			if (scglocal(scgp)->v_version == NULL)
				scglocal(scgp)->v_version = rscsiversion(scgp, f, SCG_VERSION);
			return (scglocal(scgp)->v_version);
		/*
		 * If you changed this source, you are not allowed to
		 * return "schily" for the SCG_AUTHOR request.
		 */
		case SCG_RAUTHOR:
			if (scglocal(scgp)->v_author == NULL)
				scglocal(scgp)->v_author = rscsiversion(scgp, f, SCG_AUTHOR);
			return (scglocal(scgp)->v_author);
		case SCG_RSCCS_ID:
			if (scglocal(scgp)->v_sccs_id == NULL)
				scglocal(scgp)->v_sccs_id = rscsiversion(scgp, f, SCG_SCCS_ID);
			return (scglocal(scgp)->v_sccs_id);
		}
	}
	return ((char *)0);
}

LOCAL int
scgo_ropen(scgp, device)
	SCSI	*scgp;
	char	*device;
{
		 int	busno	= scg_scsibus(scgp);
		 int	tgt	= scg_target(scgp);
		 int	tlun	= scg_lun(scgp);
	register int	f;
	register int	nopen = 0;
	char		devname[128];
	char		*p;

	if (scgp->overbose)
		error("Warning: Using remote SCSI interface.\n");

	if (busno >= MAX_SCG || tgt >= MAX_TGT || tlun >= MAX_LUN) {
		errno = EINVAL;
		if (scgp->errstr)
		    js_snprintf(scgp->errstr, SCSI_ERRSTR_SIZE,
		       "Illegal value for busno, target or lun '%d,%d,%d'",
		       busno, tgt, tlun);

		return (-1);
	}
	if (scgp->local == NULL) {
		scgp->local = malloc(sizeof(struct scg_local));
		if (scgp->local == NULL)
			return (0);
		scglocal(scgp)->remfd = -1;
		scglocal(scgp)->isopen = FALSE;
		scglocal(scgp)->rsize = 0;
		scglocal(scgp)->wsize = 0;
		scglocal(scgp)->v_version = NULL;
		scglocal(scgp)->v_author  = NULL;
		scglocal(scgp)->v_sccs_id = NULL;
	}

	if (device == NULL || (strncmp(device, "REMOTE", 6) != 0) ||
				(device = strchr(device, ':')) == NULL) {
		if (scgp->errstr)
		    js_snprintf(scgp->errstr, SCSI_ERRSTR_SIZE,
		       "Illegal remote device syntax");
		return (-1);
	}
	device++;
	/*
	 * Save non user@host:device
	 */
	js_snprintf(devname, sizeof(devname), device);

	if ((p = strchr(devname, ':')) != NULL)
		*p++ = '\0';

	f = rscsigetconn(scgp, devname);
	if (f < 0) {
		if (scgp->errstr)
		    js_snprintf(scgp->errstr, SCSI_ERRSTR_SIZE,
		       "Cannot get connection to remote host");
		return (-1);
	}
	scglocal(scgp)->remfd = f;
	debug = scgp->debug;
	if (rscsiopen(scgp, f, p) >= 0) {
		nopen++;
		scglocal(scgp)->isopen = TRUE;
	}
	return (nopen);
}

LOCAL int
scgo_rclose(scgp)
	SCSI	*scgp;
{
	register int	f;
		 int	ret;

	if (scgp->local == NULL)
		return (-1);

	if (scglocal(scgp)->v_version != NULL) {
		free(scglocal(scgp)->v_version);
		scglocal(scgp)->v_version = NULL;
	}
	if (scglocal(scgp)->v_author != NULL) {
		free(scglocal(scgp)->v_author);
		scglocal(scgp)->v_author  = NULL;
	}
	if (scglocal(scgp)->v_sccs_id != NULL) {
		free(scglocal(scgp)->v_sccs_id);
		scglocal(scgp)->v_sccs_id = NULL;
	}

	f = scglocal(scgp)->remfd;
	if (f < 0 || !scglocal(scgp)->isopen)
		return (0);
	ret = rscsiclose(scgp, f);
	scglocal(scgp)->isopen = FALSE;
	close(f);
	scglocal(scgp)->remfd = -1;
	return (ret);
}

LOCAL long
scgo_rmaxdma(scgp, amt)
	SCSI	*scgp;
	long	amt;
{
	if (scgp->local == NULL)
		return (-1L);

	return (rscsimaxdma(scgp, scglocal(scgp)->remfd, amt));
}

LOCAL void *
scgo_rgetbuf(scgp, amt)
	SCSI	*scgp;
	long	amt;
{
	int	ret;

	if (scgp->local == NULL)
		return ((void *)0);

	ret = rscsigetbuf(scgp, scglocal(scgp)->remfd, amt);
	if (ret < 0)
		return ((void *)0);

#ifdef	HAVE_VALLOC
	scgp->bufbase = (void *)valloc((size_t)amt);
#else
	scgp->bufbase = (void *)malloc((size_t)amt);
#endif
	if (scgp->bufbase == NULL) {
		scgo_rfreebuf(scgp);
		return ((void *)0);
	}
	return (scgp->bufbase);
}

LOCAL void
scgo_rfreebuf(scgp)
	SCSI	*scgp;
{
	int	f;

	if (scgp->bufbase)
		free(scgp->bufbase);
	scgp->bufbase = NULL;

	if (scgp->local == NULL)
		return;

	f = scglocal(scgp)->remfd;
	if (f < 0 || !scglocal(scgp)->isopen)
		return;
	rscsifreebuf(scgp, f);
}

LOCAL BOOL
scgo_rhavebus(scgp, busno)
	SCSI	*scgp;
	int	busno;
{
	if (scgp->local == NULL || busno < 0 || busno >= MAX_SCG)
		return (FALSE);

	return (rscsihavebus(scgp, scglocal(scgp)->remfd, busno));
}

LOCAL int
scgo_rfileno(scgp, busno, tgt, tlun)
	SCSI	*scgp;
	int	busno;
	int	tgt;
	int	tlun;
{
	int	f;

	if (scgp->local == NULL ||
	    busno < 0 || busno >= MAX_SCG ||
	    tgt < 0 || tgt >= MAX_TGT ||
	    tlun < 0 || tlun >= MAX_LUN)
		return (-1);

	f = scglocal(scgp)->remfd;
	if (f < 0 || !scglocal(scgp)->isopen)
		return (-1);
	return (rscsifileno(scgp, f, busno, tgt, tlun));
}

LOCAL int
scgo_rinitiator_id(scgp)
	SCSI	*scgp;
{
	if (scgp->local == NULL)
		return (-1);

	return (rscsiinitiator_id(scgp, scglocal(scgp)->remfd));
}

LOCAL int 
scgo_risatapi(scgp)
	SCSI	*scgp;
{
	if (scgp->local == NULL)
		return (-1);

	return (rscsiisatapi(scgp, scglocal(scgp)->remfd));
}

LOCAL int
scgo_rreset(scgp, what)
	SCSI	*scgp;
	int	what;
{
	if (scgp->local == NULL)
		return (-1);

	return (rscsireset(scgp, scglocal(scgp)->remfd, what));
}

LOCAL int
scgo_rsend(scgp)
	SCSI		*scgp;
{
	struct scg_cmd	*sp = scgp->scmd;
	int		ret;

	if (scgp->local == NULL)
		return (-1);

	if (scgp->fd < 0) {
		sp->error = SCG_FATAL;
		return (0);
	}
	ret = rscsiscmd(scgp, scglocal(scgp)->remfd, scgp->scmd);

	return (ret);
}

/*--------------------------------------------------------------------------*/
LOCAL void
rscsiabrt(sig)
	int	sig;
{
	rscsiaborted((SCSI *)0, -1);
}

LOCAL int
rscsigetconn(scgp, host)
	SCSI	*scgp;
	char	*host;
{
	static	struct servent	*sp = 0;
	static	struct passwd	*pw = 0;
		char		*name = "root";
		char		*p;
		int		rscsisock;
		char		*rscsipeer;
		char		rscsiuser[128];


	signal(SIGPIPE, rscsiabrt);
	if (sp == 0) {
		sp = getservbyname("shell", "tcp");
		if (sp == 0) {
			comerrno(EX_BAD, "shell/tcp: unknown service\n");
			/* NOTREACHED */
		}
		pw = getpwuid(getuid());
		if (pw == 0) {
			comerrno(EX_BAD, "who are you? No passwd entry found.\n");
			/* NOTREACHED */
		}
	}
	if ((p = strchr(host, '@')) != NULL) {
		js_snprintf(rscsiuser, sizeof(rscsiuser), "%.*s", p - host, host);
		name = rscsiuser;
		host = &p[1];
	} else {
		name = pw->pw_name;
	}
	if (scgp->debug)
		errmsgno(EX_BAD, "locuser: '%s' rscsiuser: '%s' host: '%s'\n",
						pw->pw_name, name, host);
	rscsipeer = host;
	rscsisock = rcmd(&rscsipeer, (unsigned short)sp->s_port,
					pw->pw_name, name, "/opt/schily/sbin/rscsi", 0);
/*					pw->pw_name, name, "/etc/xrscsi", 0);*/

	return (rscsisock);
}

LOCAL char *
rscsiversion(scgp, fd, what)
	SCSI	*scgp;
	int	fd;
	int	what;
{
	char	cbuf[CMD_SIZE];
	char	*p;
	int	ret;

	js_snprintf(cbuf, sizeof(cbuf), "V%d\n", what);
	ret = rscsicmd(scgp, fd, "version", cbuf);
	p = malloc(ret);
	if (p == NULL)
		return (p);
	rscsireadbuf(scgp, fd, p, ret);
	return (p);
}

LOCAL int
rscsiopen(scgp, fd, fname)
	SCSI	*scgp;
	int	fd;
	char	*fname;
{
	char	cbuf[CMD_SIZE];
	int	ret;
	int	bus;
	int	chan;
	int	tgt;
	int	lun;

	js_snprintf(cbuf, sizeof(cbuf), "O%s\n", fname?fname:"");
	ret = rscsicmd(scgp, fd, "open", cbuf);
	if (ret < 0)
		return (ret);

	bus = rscsireadnum(scgp, fd);
	chan = rscsireadnum(scgp, fd);	
	tgt = rscsireadnum(scgp, fd);
	lun = rscsireadnum(scgp, fd);

	scg_settarget(scgp, bus, tgt, lun);
	return (ret);
}

LOCAL int
rscsiclose(scgp, fd)
	SCSI	*scgp;
	int	fd;
{
	return (rscsicmd(scgp, fd, "close", "C\n"));
}

LOCAL int
rscsimaxdma(scgp, fd, amt)
	SCSI	*scgp;
	int	fd;
	long	amt;
{
	char	cbuf[CMD_SIZE];

	js_snprintf(cbuf, sizeof(cbuf), "D%ld\n", amt);
	return (rscsicmd(scgp, fd, "maxdma", cbuf));
}

LOCAL int
rscsigetbuf(scgp, fd, amt)
	SCSI	*scgp;
	int	fd;
	long	amt;
{
	char	cbuf[CMD_SIZE];
	int	size;
	int	ret;

	js_snprintf(cbuf, sizeof(cbuf), "M%ld\n", amt);
	ret = rscsicmd(scgp, fd, "getbuf", cbuf);
	if (ret < 0)
		return (ret);

	size = ret + 1024;	/* Add protocol overhead */

#ifdef	SO_SNDBUF
	if (size > scglocal(scgp)->wsize) while (size > 512 &&
		setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
					(char *)&size, sizeof (size)) < 0) {
		size -= 512;
	}
	if (size > scglocal(scgp)->wsize) {
		scglocal(scgp)->wsize = size;
		if (scgp->debug)
			errmsgno(EX_BAD, "sndsize: %d\n", size);
	}
#endif
#ifdef	SO_RCVBUF
	if (size > scglocal(scgp)->rsize) while (size > 512 &&
		setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
					(char *)&size, sizeof (size)) < 0) {
		size -= 512;
	}
	if (size > scglocal(scgp)->rsize) {
		scglocal(scgp)->rsize = size;
		if (scgp->debug)
			errmsgno(EX_BAD, "rcvsize: %d\n", size);
	}
#endif
	return (ret);
}

LOCAL int
rscsifreebuf(scgp, fd)
	SCSI	*scgp;
	int	fd;
{
	return (rscsicmd(scgp, fd, "freebuf", "F\n"));
}

LOCAL int
rscsihavebus(scgp, fd, busno)
	SCSI	*scgp;
	int	fd;
	int	busno;
{
	char	cbuf[2*CMD_SIZE];

	js_snprintf(cbuf, sizeof(cbuf), "B%d\n%d\n",
		busno,
		0);
	return (rscsicmd(scgp, fd, "havebus", cbuf));
}

LOCAL int
rscsifileno(scgp, fd, busno, tgt, tlun)
	SCSI	*scgp;
	int	fd;
	int	busno;
	int	tgt;
	int	tlun;
{
	char	cbuf[3*CMD_SIZE];

	js_snprintf(cbuf, sizeof(cbuf), "T%d\n%d\n%d\n%d\n",
		busno,
		0,
		tgt,
		tlun);
	return (rscsicmd(scgp, fd, "fileno", cbuf));
}

LOCAL int
rscsiinitiator_id(scgp, fd)
	SCSI	*scgp;
	int	fd;
{
	return (rscsicmd(scgp, fd, "initiator id", "I\n"));
}

LOCAL int
rscsiisatapi(scgp, fd)
	SCSI	*scgp;
	int	fd;
{
	return (rscsicmd(scgp, fd, "isatapi", "A\n"));
}

LOCAL int
rscsireset(scgp, fd, what)
	SCSI	*scgp;
	int	fd;
	int	what;
{
	char	cbuf[CMD_SIZE];

	js_snprintf(cbuf, sizeof(cbuf), "R%d\n", what);
	return (rscsicmd(scgp, fd, "reset", cbuf));
}

LOCAL int
rscsiscmd(scgp, fd, sp)
	SCSI	*scgp;
	int	fd;
	struct scg_cmd  *sp;
{
	char	cbuf[1600];
	int	ret;
	int	amt = 0;
	int	voidsize = 0;

	ret = js_snprintf(cbuf, sizeof(cbuf), "S%d\n%d\n%d\n%d\n%d\n",
		sp->size, sp->flags,
		sp->cdb_len, sp->sense_len,
		sp->timeout);
	movebytes(sp->cdb.cmd_cdb, &cbuf[ret], sp->cdb_len);
	ret += sp->cdb_len;

	if ((sp->flags & SCG_RECV_DATA) == 0 && sp->size > 0) {
		amt = sp->size;
		if ((ret + amt) <= sizeof(cbuf)) {
			movebytes(sp->addr, &cbuf[ret], amt);
			ret += amt;
			amt = 0;
		}
	}
	errno = 0;
	if (write(fd, cbuf, ret) != ret)
		rscsiaborted(scgp, fd);
	
	if (amt > 0) {
		if (write(fd, sp->addr, amt) != amt)
			rscsiaborted(scgp, fd);
	}

	ret = rscsigetstatus(scgp, fd, "sendcmd");
	if (ret < 0)
		return (ret);

	sp->resid = sp->size - ret;
	sp->error = rscsireadnum(scgp, fd);
	sp->ux_errno = rscsireadnum(scgp, fd);
	*(Uchar *)&sp->scb = rscsireadnum(scgp, fd);
	sp->sense_count = rscsireadnum(scgp, fd);

	if (sp->sense_count > SCG_MAX_SENSE) {
		voidsize = sp->sense_count - SCG_MAX_SENSE;
		sp->sense_count = SCG_MAX_SENSE;
	}
	if (sp->sense_count > 0) {
		rscsireadbuf(scgp, fd, (char *)sp->u_sense.cmd_sense, sp->sense_count);
		rscsivoidarg(scgp, fd, voidsize);
	}

	if ((sp->flags & SCG_RECV_DATA) != 0 && ret > 0) 
		rscsireadbuf(scgp, fd, sp->addr, ret);

	return (0);
}

LOCAL int
rscsireadbuf(scgp, fd, buf, count)
	SCSI	*scgp;
	int	fd;
	char	*buf;
	int	count;
{
	register int	n = count;
	register int	amt = 0;
	register int	cnt;

	while (amt < n) {
		if ((cnt = read(fd, &buf[amt], n - amt)) <= 0) {
			return (rscsiaborted(scgp, fd));
		}
		amt += cnt;
	}
	return (amt);
}

LOCAL void
rscsivoidarg(scgp, fd, n)
	SCSI	*scgp;
	int	fd;
	register int	n;
{
	register int	i;
	register int	amt;
		 char	buf[512];

	for (i = 0; i < n; i += amt) {
		amt = sizeof(buf);
		if ((n - i) < amt)
			amt = n - i;
		rscsireadbuf(scgp, fd, buf, amt);
	}
}

LOCAL int
rscsicmd(scgp, fd, name, cbuf)
	SCSI	*scgp;
	int	fd;
	char	*name;
	char	*cbuf;
{
	rscsisendcmd(scgp, fd, name, cbuf);
	return (rscsigetstatus(scgp, fd, name));
}

LOCAL void
rscsisendcmd(scgp, fd, name, cbuf)
	SCSI	*scgp;
	int	fd;
	char	*name;
	char	*cbuf;
{
	int	buflen = strlen(cbuf);

	errno = 0;
	if (write(fd, cbuf, buflen) != buflen)
		rscsiaborted(scgp, fd);
}

LOCAL int
rscsigetline(scgp, fd, line, count)
	SCSI	*scgp;
	int	fd;
	char	*line;
	int	count;
{
	register char	*cp;

	for (cp = line; cp < &line[count]; cp++) {
		if (read(fd, cp, 1) != 1)
			return (rscsiaborted(scgp, fd));

		if (*cp == '\n') {
			*cp = '\0';
			return (cp - line);
		}
	}
	return (rscsiaborted(scgp, fd));
}

LOCAL int
rscsireadnum(scgp, fd)
	SCSI	*scgp;
	int	fd;
{
	char	cbuf[CMD_SIZE];

	rscsigetline(scgp, fd, cbuf, sizeof(cbuf));
	return (atoi(cbuf));
}

LOCAL int
rscsigetstatus(scgp, fd, name)
	SCSI	*scgp;
	int	fd;
	char	*name;
{
	char	cbuf[CMD_SIZE];
	char	code;
	int	number;
	int	count;
	int	voidsize = 0;

	rscsigetline(scgp, fd, cbuf, sizeof(cbuf));
	code = cbuf[0];
	number = atoi(&cbuf[1]);

	if (code == 'E' || code == 'F') {
		rscsigetline(scgp, fd, cbuf, sizeof(cbuf));
		if (code == 'F')	/* should close file ??? */
			rscsiaborted(scgp, fd);

		rscsigetline(scgp, fd, cbuf, sizeof(cbuf));
		count = atoi(cbuf);
		if (count > 0) {
			if (scgp->errstr == NULL) {
				voidsize = count;
				count = 0;
			} else if (count > SCSI_ERRSTR_SIZE) {
				voidsize = count - SCSI_ERRSTR_SIZE;
				count = SCSI_ERRSTR_SIZE;
			}
			rscsireadbuf(scgp, fd, scgp->errstr, count);
			rscsivoidarg(scgp, fd, voidsize);
		}
		if (scgp->debug)
			errmsgno(number, "Remote status(%s): %d '%s'.\n",
							name, number, cbuf);
		errno = number;
		return (-1);
	}
	if (code != 'A') {
		/* XXX Hier kommt evt Command not found ... */
		if (scgp->debug)
			errmsgno(EX_BAD, "Protocol error (got %s).\n", cbuf);
		return (rscsiaborted(scgp, fd));
	}
	return (number);
}

LOCAL int
rscsiaborted(scgp, fd)
	SCSI	*scgp;
	int	fd;
{
	if ((scgp && scgp->debug) || debug)
		errmsgno(EX_BAD, "Lost connection to remote host ??\n");
	/* if fd >= 0 */
	/* close file */
	if (errno == 0)
		errno = EIO;
	return (-1);
}
#endif	/* USE_REMOTE */