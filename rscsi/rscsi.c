/* @(#)rscsi.c	1.12 01/04/21 Copyright 1994,2000 J. Schilling*/
#ifndef lint
static	char sccsid[] =
	"@(#)rscsi.c	1.12 01/04/21 Copyright 1994,2000 J. Schilling";
#endif
/*
 *	Remote SCSI server
 *
 *	Copyright (c) 1994,2000 J. Schilling
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

/*#define	FORCE_DEBUG*/

#include <mconfig.h>
#include <stdio.h>
#include <stdxlib.h>
#include <unixstd.h>
#include <fctldefs.h>
#include <strdefs.h>
#include <sys/types.h>
#include <sys/socket.h>
#ifdef	 HAVE_SYS_PARAM_H
#include <sys/param.h>	/* BSD-4.2 & Linux need this for MAXHOSTNAMELEN */
#endif
#include <errno.h>
#include <pwd.h>

#include <utypes.h>
#include <standard.h>
#include <deflts.h>
#include <patmatch.h>
#include <schily.h>

#include <scg/scgcmd.h>
#include <scg/scsitransp.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifndef	HAVE_ERRNO_DEF
extern	int	errno;
#endif

EXPORT	int	main		__PR((int argc, char **argv));
LOCAL	void	checkuser	__PR((void));
LOCAL	char	*getpeer	__PR((void));
LOCAL	BOOL	checktarget	__PR((void));
LOCAL	BOOL	strmatch	__PR((char *str, char *pat));
LOCAL	void	dorscsi		__PR((void));
LOCAL	void	scsiversion	__PR((void));
LOCAL	void	openscsi	__PR((void));
LOCAL	void	closescsi	__PR((void));
LOCAL	void	maxdma		__PR((void));
LOCAL	void	getbuf		__PR((void));
LOCAL	void	freebuf		__PR((void));
LOCAL	void	havebus		__PR((void));
LOCAL	void	scsifileno	__PR((void));
LOCAL	void	initiator_id	__PR((void));
LOCAL	void	isatapi		__PR((void));
LOCAL	void	scsireset	__PR((void));
LOCAL	void	sendcmd		__PR((void));
LOCAL	void	readbuf		__PR((char *buf, int n));
LOCAL	void	voidarg		__PR((int n));
LOCAL	void	readarg		__PR((char *buf, int n));
LOCAL	char *	preparebuffer	__PR((int size));
LOCAL	int	checkscsi	__PR((char *decive));
LOCAL	void	rscsirespond	__PR((int ret, int err));
LOCAL	void	rscsireply	__PR((int ret));
LOCAL	void	rscsierror	__PR((int err, char *str, char *xstr));

#define	CMD_SIZE	80

LOCAL	SCSI	*scsi_ptr = NULL;
LOCAL	char	*Sbuf;
LOCAL	long	Sbufsize;

LOCAL	char	*username;
LOCAL	char	*peername;

LOCAL	char	*debug_name;
LOCAL	FILE	*debug_file;

#define	DEBUG(fmt)		if (debug_file) js_fprintf(debug_file, fmt)
#define	DEBUG1(fmt,a)		if (debug_file) js_fprintf(debug_file, fmt, a)
#define	DEBUG2(fmt,a1,a2)	if (debug_file) js_fprintf(debug_file, fmt, a1, a2)
#define	DEBUG3(fmt,a1,a2,a3)	if (debug_file) js_fprintf(debug_file, fmt, a1, a2, a3)
#define	DEBUG4(fmt,a1,a2,a3,a4)	if (debug_file) js_fprintf(debug_file, fmt, a1, a2, a3, a4)
#define	DEBUG5(fmt,a1,a2,a3,a4,a5)	if (debug_file) js_fprintf(debug_file, fmt, a1, a2, a3, a4, a5)
#define	DEBUG6(fmt,a1,a2,a3,a4,a5,a6)	if (debug_file) js_fprintf(debug_file, fmt, a1, a2, a3, a4, a5, a6)

EXPORT int
main(argc, argv)
	int	argc;
	char	**argv;
{
	save_args(argc, argv);
	argc--, argv++;
	if (argc > 0 && strcmp(*argv, "-c") == 0) {
		/*
		 * Skip params in case we have been installed as shell.
		 */
		argc--, argv++;
		argc--, argv++;
	}
	if (defltopen("/etc/default/rscsi") < 0) {
		rscsierror(errno, errmsgstr(errno),
			"Remote configuration error: Cannot open /etc/default/rscsi");
/*		rscsirespond(-1, errno);*/
		exit(EX_BAD);
	}
	debug_name=defltread("DEBUG=");
#ifdef	FORCE_DEBUG
	if (debug_name == NULL && argc <= 0)
		debug_name = "/tmp/RSCSI";
#endif
	if (argc > 0)
		debug_name = *argv;

	if (debug_name != NULL)
		debug_file = fopen(debug_name, "w");
		
	if (argc > 0) {
		if (debug_file == 0) {
			rscsirespond(-1, errno);
			exit(EX_BAD);
		}
		(void) setbuf(debug_file, (char *)0);
	}
	checkuser();		/* Check if we are called by a bad guy	*/
	peername = getpeer();	/* Get host name of caller		*/
	dorscsi();
	return (0);
}

LOCAL void
checkuser()
{
	uid_t	uid = getuid();
	char	*uname;
	struct passwd *pw;

	if (uid == 0) {
		username = "root";
		return;
	}
	pw = getpwuid(uid);
	if (pw == NULL)
		goto notfound;

	username = pw->pw_name;

	defltfirst();
	while ((uname = defltnext("USER=")) != NULL) {
		if (strmatch(username, uname))
			return;
	}
notfound:
	DEBUG2("rscsid: Illegal user '%s' id %ld for RSCSI server\n",
						username, (long)uid);
	rscsierror(0, "Illegal user id for RSCSI server", NULL);
	exit(EX_BAD);
}

LOCAL char *
getpeer()
{
	struct	sockaddr sa;
	struct	sockaddr_in *s;
	socklen_t	 sasize = sizeof(sa);
	struct hostent	*he;
static	char		buffer[MAXHOSTNAMELEN];

	if (getpeername(STDIN_FILENO, &sa, &sasize) < 0) {
		DEBUG1("rscsid: peername %s\n", errmsgstr(errno));
		return ("ILLEGAL_SOCKET");
	} else {
		s = (struct sockaddr_in *)&sa;
		if (s->sin_family != AF_INET)
			return ("NOT_IP");
               
		(void) js_snprintf(buffer, sizeof(buffer), "%s", inet_ntoa(s->sin_addr));
		DEBUG1("rscsid: peername %s\n", buffer);
		he = gethostbyaddr((char *)&s->sin_addr.s_addr, 4, AF_INET);
		if (he != NULL)
			(void) js_snprintf(buffer, sizeof(buffer), "%s", he->h_name);
		DEBUG1("rscsid: peername %s\n", buffer);
		return (buffer);
	}
}

LOCAL BOOL
checktarget()
{
	char	*target;
	char	*user;
	char	*host;
	char	*p;
	int	bus;
	int	chan;
	int	tgt;
	int	lun;

	if (peername == NULL)
		return (FALSE);
	defltfirst();
	while ((target = defltnext("ACCESS=")) != NULL) {
		p = target;
		while (*p == '\t')
			p++;
		user = p;
		if ((p = strchr(p, '\t')) != NULL)
			*p++ = '\0';
		else
			continue;
		if (!strmatch(username, user))
			continue;

		while (*p == '\t')
			p++;
		host = p;
		if ((p = strchr(p, '\t')) != NULL)
			*p++ = '\0';
		else
			continue;
		if (!strmatch(peername, host))
			continue;

		p = astoi(p, &bus);
		if (*p != '\t')
			continue;
		p = astoi(p, &chan);
		if (*p != '\t')
			continue;
		p = astoi(p, &tgt);
		if (*p != '\t')
			continue;
		p = astoi(p, &lun);

		if (*p != '\t' && *p != '\n' && *p != '\r' && *p != '\0') 
			continue;
		DEBUG6("ACCESS %s %s %d.%d,%d,%d\n", user, host, bus, chan, tgt, lun);

		if (bus != -1 && bus != scg_scsibus(scsi_ptr))
			continue;
		if (tgt != -1 && tgt != scg_target(scsi_ptr))
			continue;
		if (lun != -1 && lun != scg_lun(scsi_ptr))
			continue;
		return (TRUE);
	}
	return (FALSE);
}

LOCAL BOOL
strmatch(str, pat)
	char	*str;
	char	*pat;
{
	int	*aux;
	int	*state;
	int	alt;
	int	plen;
	char	*p;

	plen = strlen(pat);
	aux = malloc(plen*sizeof(int));
	state = malloc((plen+1)*sizeof(int));
	if (aux == NULL || state == NULL) {
		if (aux) free(aux);
		if (state) free(state);
		return (FALSE);
	}

	if ((alt = patcompile((const unsigned char *)pat, plen, aux)) == 0) {
		/* Bad pattern */
		free(aux);
		free(state);
		return (FALSE);
	}

	p = (char *)patmatch((const unsigned char *)pat, aux,
							(const unsigned char *)str, 0,
							strlen(str), alt, state);
	free(aux);
	free(state);

	if (p != NULL && *p == '\0')
		return (TRUE);
	return (FALSE);
}

LOCAL void
dorscsi()
{
	char	c;

	while (read(STDIN_FILENO, &c, 1) == 1) {
		errno = 0;

		switch (c) {

		case 'V':		/* "V" ersion	*/
			scsiversion();
			break;
		case 'O':		/* "O" pen	*/
			openscsi();
			break;
		case 'C':		/* "C" lose	*/
			closescsi();
			break;
		case 'D':		/* "D" MA	*/
			maxdma();
			break;
		case 'M':		/* "M" alloc	*/
			getbuf();
			break;
		case 'F':		/* "F" free	*/
			freebuf();
			break;
		case 'B':		/* "B" us	*/
			havebus();
			break;
		case 'T':		/* "T" arget	*/
			scsifileno();
			break;
		case 'I':		/* "I" nitiator	*/
			initiator_id();
			break;
		case 'A':		/* "A" tapi	*/
			isatapi();
			break;
		case 'R':		/* "R" eset	*/
			scsireset();
			break;
		case 'S':		/* "S" end	*/
			sendcmd();
			break;

		default:
			DEBUG1("rscsid: garbage command '%c'\n", c);
			rscsierror(0, "Garbage command", NULL);
			exit(EX_BAD);
		}
	}
	exit(0);
}

LOCAL void
scsiversion()
{
	int	ret;
	char	*str;
	char	what[CMD_SIZE];

	readarg(what, sizeof(what));
	DEBUG1("rscsid: V %s\n", what);
	if (scsi_ptr == NULL) {
		rscsirespond(-1, EBADF);
		return;
	}
	str = scg_version(scsi_ptr, atoi(what));
	ret = strlen(str);
	ret++;	/* Include null char */
	rscsirespond(ret, errno);
	write(STDOUT_FILENO, str, ret);
}

LOCAL void
openscsi()
{
	char	device[CMD_SIZE];
	char	errstr[80];
	int	debug = 0;
	int	lverbose = 0;
	int	ret = 0;
	char	rbuf[1600];

	if (scsi_ptr != NULL)
		(void) scg_close(scsi_ptr);

	readarg(device, sizeof(device));
	DEBUG1("rscsid: O %s\n", device);
	if (strncmp(device, "REMOTE", 6) == 0) {
		scsi_ptr = NULL;
		errno = EINVAL;
	} else if (!checkscsi(device)) {
		scsi_ptr = NULL;
		errno = EACCES;
	} else {
		scsi_ptr = scg_open(device, errstr, sizeof(errstr), debug, lverbose);
		if (scsi_ptr == NULL) {
			ret = -1;
		} else {
			scsi_ptr->silent = 1;
			scsi_ptr->verbose = 0;
			scsi_ptr->debug = 0;
			scsi_ptr->kdebug = 0;
		}
	}
	if (ret < 0) {
		/*
		 * XXX This is currently the only place where we use the
		 * XXX extended error string.
		 */
		rscsierror(errno, errmsgstr(errno), errstr);
/*		rscsirespond(ret, errno);*/
		return;
	}
	DEBUG4("rscsid:>A 0 %d.%d,%d,%d\n", 
		scg_scsibus(scsi_ptr),
		0,
		scg_target(scsi_ptr),
		scg_lun(scsi_ptr));

	ret = js_snprintf(rbuf, sizeof(rbuf), "A0\n%d\n%d\n%d\n%d\n",
		scg_scsibus(scsi_ptr),
		0,
		scg_target(scsi_ptr),
		scg_lun(scsi_ptr));
	(void) write(STDOUT_FILENO, rbuf, ret);
}

LOCAL void
closescsi()
{
	int	ret;
	char	device[CMD_SIZE];

	readarg(device, sizeof(device));
	DEBUG1("rscsid: C %s\n", device);
	ret = scg_close(scsi_ptr);
	rscsirespond(ret, errno);
	scsi_ptr = NULL;
}

LOCAL void
maxdma()
{
	int	ret;
	char	amt[CMD_SIZE];

	readarg(amt, sizeof(amt));
	DEBUG1("rscsid: D %s\n", amt);
	if (scsi_ptr == NULL) {
		rscsirespond(-1, EBADF);
		return;
	}
	ret = scg_bufsize(scsi_ptr, atol(amt));
	rscsirespond(ret, errno);
}

LOCAL void
getbuf()
{
	int	ret = 0;
	char	amt[CMD_SIZE];

	readarg(amt, sizeof(amt));
	DEBUG1("rscsid: M %s\n", amt);
	if (scsi_ptr == NULL) {
		rscsirespond(-1, EBADF);
		return;
	}
	ret = scg_bufsize(scsi_ptr, atol(amt));
	if (preparebuffer(ret) == NULL)
		ret = -1;
	rscsirespond(ret, errno);
}

LOCAL void
freebuf()
{
	int	ret = 0;
	char	dummy[CMD_SIZE];

	readarg(dummy, sizeof(dummy));
	DEBUG1("rscsid: F %s\n", dummy);
	if (scsi_ptr == NULL) {
		rscsirespond(-1, EBADF);
		return;
	}
	scg_freebuf(scsi_ptr);
	Sbuf = NULL;
	rscsirespond(ret, errno);
}

LOCAL void
havebus()
{
	int	ret;
	char	bus[CMD_SIZE];
	char	chan[CMD_SIZE];

	readarg(bus, sizeof(bus));
	readarg(chan, sizeof(chan));
	DEBUG2("rscsid: B %s.%s\n", bus, chan);
	if (scsi_ptr == NULL) {
		rscsirespond(-1, EBADF);
		return;
	}
	ret = scg_havebus(scsi_ptr, atol(bus));
	rscsirespond(ret, errno);
}

LOCAL void
scsifileno()
{
	int	ret;
	char	bus[CMD_SIZE];
	char	chan[CMD_SIZE];
	char	tgt[CMD_SIZE];
	char	lun[CMD_SIZE];

	readarg(bus, sizeof(bus));
	readarg(chan, sizeof(chan));
	readarg(tgt, sizeof(tgt));
	readarg(lun, sizeof(lun));
	DEBUG4("rscsid: T %s.%s,%s,%s\n", bus, chan, tgt, lun);
	if (scsi_ptr == NULL) {
		rscsirespond(-1, EBADF);
		return;
	}
	errno = 0;
	ret = scg_settarget(scsi_ptr, atoi(bus), atoi(tgt), atoi(lun));
	if (!checktarget()) {
		scg_settarget(scsi_ptr, -1, -1, -1);
		ret = -1;
	}
	if (errno != 0)
		rscsirespond(ret, errno);
	else
		rscsireply(ret);
}

LOCAL void
initiator_id()
{
	int	ret;
	char	dummy[CMD_SIZE];

	readarg(dummy, sizeof(dummy));
	DEBUG1("rscsid: I %s\n", dummy);
	if (scsi_ptr == NULL) {
		rscsirespond(-1, EBADF);
		return;
	}
	errno = 0;
	ret = scg_initiator_id(scsi_ptr);
	if (errno != 0)
		rscsirespond(ret, errno);
	else
		rscsireply(ret);
}

LOCAL void
isatapi()
{
	int	ret;
	char	dummy[CMD_SIZE];

	readarg(dummy, sizeof(dummy));
	DEBUG1("rscsid: A %s\n", dummy);
	if (scsi_ptr == NULL) {
		rscsirespond(-1, EBADF);
		return;
	}
	errno = 0;
	ret = scg_isatapi(scsi_ptr);
	if (errno != 0)
		rscsirespond(ret, errno);
	else
		rscsireply(ret);
}

LOCAL void
scsireset()
{
	int	ret;
	char	what[CMD_SIZE];

	readarg(what, sizeof(what));
	DEBUG1("rscsid: R %s\n", what);
	if (scsi_ptr == NULL) {
		rscsirespond(-1, EBADF);
		return;
	}
	ret = scg_reset(scsi_ptr, atoi(what));
	rscsirespond(ret, errno);
}

LOCAL void
sendcmd()
{
	register struct	scg_cmd	*scmd;
	int	n;
	int	ret;
	char	count[CMD_SIZE];
	char	flags[CMD_SIZE];
	char	cdb_len[CMD_SIZE];
	char	sense_len[CMD_SIZE];
	char	timeout[CMD_SIZE];
	int	csize;
	int	cflags;
	int	clen;
	int	ctimeout;
	char	rbuf[1600];
	char	*p;

	/*
	 *	S count\n
	 *	flags\n
	 *	cdb_len\n
	 *	sense_len\n
	 *	timeout\n
	 *	<data if available>
	 *
	 *	Timeout:
	 *	-	sss	(e.g. 10)
	 *	-	sss.uuu	(e.g. 10.23)
	 */
	readarg(count, sizeof(count));
	readarg(flags, sizeof(flags));
	readarg(cdb_len, sizeof(cdb_len));
	readarg(sense_len, sizeof(sense_len));
	readarg(timeout, sizeof(timeout));
	DEBUG5("rscsid: S %s %s %s %s %s", count, flags, cdb_len, sense_len, timeout);
	csize = atoi(count);
	cflags = atoi(flags);
	clen = atoi(cdb_len);

	p = strchr(timeout, '.');
	if (p)
		*p = '\0';
	ctimeout = atoi(timeout);

	if (scsi_ptr == NULL || clen > SCG_MAX_CMD || csize > Sbufsize) {
		DEBUG("\n");
		voidarg(clen);
		if ((cflags & SCG_RECV_DATA) == 0 && csize > 0)
			voidarg(csize);
		rscsirespond(-1, scsi_ptr==NULL ? EBADF : EINVAL);
		return;
	}

	scmd = scsi_ptr->scmd;
	fillbytes((caddr_t)scmd, sizeof(*scmd), '\0');
	scmd->addr = (caddr_t)Sbuf;
	scmd->size = csize;
	scmd->flags = cflags;
	scmd->cdb_len = clen;
	scmd->sense_len = atoi(sense_len);
	scmd->timeout = ctimeout;
	readbuf((char *)scmd->cdb.cmd_cdb, clen);
	DEBUG6(" 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
		scmd->cdb.cmd_cdb[0],
		scmd->cdb.cmd_cdb[1],
		scmd->cdb.cmd_cdb[2],
		scmd->cdb.cmd_cdb[3],
		scmd->cdb.cmd_cdb[4],
		scmd->cdb.cmd_cdb[5]);

	if ((cflags & SCG_RECV_DATA) == 0 && csize > 0)
		readbuf(Sbuf, scmd->size);

	scsi_ptr->cmdname = "";

	ret = scg_cmd(scsi_ptr);

	n = 0;
	if ((csize - scmd->resid) > 0)
		n = csize - scmd->resid;

	/*
	 *	A count\n
	 *	error\n
	 *	errno\n
	 *	scb\n
	 *	sense_count\n
	 *	<data if available>
	 */
	DEBUG5("rscsid:>A %d %d %d %d %d\n",
		n,
		scmd->error,
		scmd->ux_errno,
		*(Uchar *)&scmd->scb,
		scmd->sense_count);

	ret = js_snprintf(rbuf, sizeof(rbuf), "A%d\n%d\n%d\n%d\n%d\n",
		n,
		scmd->error,
		scmd->ux_errno,
		*(Uchar *)&scmd->scb,
		scmd->sense_count);

	if (scmd->sense_count > 0) {
		movebytes(scmd->u_sense.cmd_sense, &rbuf[ret], scmd->sense_count);
		ret += scmd->sense_count;
	}
	if ((cflags & SCG_RECV_DATA) == 0)
		n = 0;
	if (n > 0 && ((ret + n) <= sizeof(rbuf))) {
		movebytes(Sbuf, &rbuf[ret], n);
		ret += n;
		n = 0;
	}
	(void) write(STDOUT_FILENO, rbuf, ret);

	if (n > 0)
		(void) write(STDOUT_FILENO, Sbuf, n);
}

LOCAL void
readbuf(buf, n)
	register char	*buf;
	register int	n;
{
	register int	i;
	register int	amt;

	for (i = 0; i < n; i += amt) {
		amt = read(STDIN_FILENO, &buf[i], n - i);
		if (amt <= 0) {
			DEBUG("rscsid: premature eof\n");
			rscsierror(0, "Premature eof", NULL);
			exit(EX_BAD);
		}
	}
}

LOCAL void
voidarg(n)
	register int	n;
{
	register int	i;
	register int	amt;
		 char	buf[512];

	for (i = 0; i < n; i += amt) {
		amt = sizeof(buf);
		if ((n - i) < amt)
			amt = n - i;
		readbuf(buf, amt);
	}
}

LOCAL void
readarg(buf, n)
	char	*buf;
	int	n;
{
	int	i;

	for (i = 0; i < n; i++) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1)
			exit(0);
		if (buf[i] == '\n')
			break;
	}
	buf[i] = '\0';
}

LOCAL char *
preparebuffer(size)
	int	size;
{
	Sbufsize = size;
	if ((Sbuf = scg_getbuf(scsi_ptr, Sbufsize)) == NULL) {
		Sbufsize = 0L;
		return (Sbuf);
	}
	size = Sbufsize + 1024;	/* Add protocol overhead */

#ifdef	SO_SNDBUF
	while (size > 512 &&
	       setsockopt(STDOUT_FILENO, SOL_SOCKET, SO_SNDBUF, (char *)&size, sizeof (size)) < 0)
		size -= 512;
	DEBUG1("rscsid: sndsize: %d\n", size);
#endif
#ifdef	SO_RCVBUF
	while (size > 512 &&
	       setsockopt(STDIN_FILENO, SOL_SOCKET, SO_RCVBUF, (char *)&size, sizeof (size)) < 0)
		size -= 512;
	DEBUG1("rscsid: rcvsize: %d\n", size);
#endif
	return (Sbuf);
}

LOCAL int
checkscsi(device)
	char	*device;
{
#ifdef	CHECKTAPE
	if (strncmp(device, "/dev/rst", 8) == 0 ||
	    strncmp(device, "/dev/nrst", 9) == 0 ||
	    strcmp(device, "/dev/zero") == 0 ||
	    strcmp(device, "/dev/null") == 0)
		return (1);
	return (0);
#else
	return (1);
#endif
}

LOCAL void
rscsirespond(ret, err)
	int	ret;
	int	err;
{
	if (ret < 0) {
		rscsierror(err, errmsgstr(err), NULL);
	} else {
		rscsireply(ret);
	}
}

LOCAL void
rscsireply(ret)
	int	ret;
{
	char	rbuf[CMD_SIZE];

	DEBUG1("rscsid:>A %d\n", ret);
	(void) js_snprintf(rbuf, sizeof(rbuf), "A%d\n", ret);
	(void) write(STDOUT_FILENO, rbuf, strlen(rbuf));
}

LOCAL void
rscsierror(err, str, xstr)
	int	err;
	char	*str;
	char	*xstr;
{
	char	rbuf[1600];
	int	xlen = 0;
	int	n;

	if (xstr != NULL)
		xlen = strlen(xstr) + 1;

	DEBUG3("rscsid:>E %d (%s) [%s]\n", err, str, xstr?xstr:"");
	n = js_snprintf(rbuf, sizeof(rbuf), "E%d\n%s\n%d\n", err, str, xlen);

	if (xlen > 0 && ((xlen + n) <= sizeof(rbuf))) {
		movebytes(xstr, &rbuf[n], xlen);
		n += xlen;
		xlen = 0;
	}
	(void) write(STDOUT_FILENO, rbuf, n);
	if (xlen > 0)
		(void) write(STDOUT_FILENO, xstr, xlen);
}