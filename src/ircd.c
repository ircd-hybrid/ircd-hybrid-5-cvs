/************************************************************************
 *   IRC - Internet Relay Chat, src/ircd.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef lint
static	char sccsid[] = "@(#)ircd.c	2.48 3/9/94 (C) 1988 University of Oulu, \
Computing Center and Jarkko Oikarinen";
static char *rcs_version="$Id: ircd.c,v 1.19 1998/02/12 21:37:24 mpearce Exp $";
#endif

#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pwd.h>
#include <signal.h>
#include <fcntl.h>
#include "inet.h"
#include "h.h"

#include "dich_conf.h"

/* Lists to do K: line matching -Sol */
aConfList	KList1 = { 0, NULL };	/* ordered */
aConfList	KList2 = { 0, NULL };	/* ordered, reversed */
aConfList	KList3 = { 0, NULL };	/* what we can't sort */

aConfList	BList1 = { 0, NULL };	/* ordered */
aConfList	BList2 = { 0, NULL };	/* ordered, reversed */
aConfList	BList3 = { 0, NULL };	/* what we can't sort */

aConfList	DList1 = { 0, NULL };	/* ordered */

aConfList       EList1 = { 0, NULL };   /* ordered */
aConfList       EList2 = { 0, NULL };   /* ordered, reversed */
aConfList       EList3 = { 0, NULL };   /* what we can't sort */

aConfList       FList1 = { 0, NULL };   /* ordered */
aConfList       FList2 = { 0, NULL };   /* ordered, reversed */
aConfList       FList3 = { 0, NULL };   /* what we can't sort */

aMotd		*motd;
aMotd		*helpfile;	/* misnomer, aMotd could be generalized */
struct tm	*motd_tm;

/* this stuff by mnystrom@mit.edu */
#include "fdlist.h"

#ifdef SETUID_ROOT
#include <sys/lock.h>
#include <sys/types.h>
#include <unistd.h>
#endif /* SETUID_ROOT */

fdlist serv_fdlist;
fdlist oper_fdlist;
fdlist listen_fdlist;

#ifndef NO_PRIORITY
fdlist busycli_fdlist;	/* high-priority clients */
#endif

fdlist default_fdlist;	/* just the number of the entry */
/*    */

int	MAXCLIENTS = MAX_CLIENTS;	/* semi-configurable if QUOTE_SET is def*/
struct	Counter	Count;
int	R_do_dns, R_fin_dns, R_fin_dnsc, R_fail_dns,
	R_do_id, R_fin_id, R_fail_id;

time_t	NOW;
aClient me;			/* That's me */
aClient *client = &me;		/* Pointer to beginning of Client list */
#ifdef  LOCKFILE
extern  time_t  pending_kline_time;
extern	struct pkl *pending_klines;
extern  void do_pending_klines(void);
#endif

void	server_reboot();
void	restart (char *);
static	void	open_debugfile(), setup_signals();
static  time_t	io_loop(time_t);

/* externally needed functions */

extern  void init_fdlist(fdlist *);	/* defined in fdlist.c */
extern	void dbuf_init();		/* defined in dbuf.c */
extern  void   read_motd(char *);	/* defined in s_serv.c */
extern  void   read_help(char *);	/* defined in s_serv.c */

char	**myargv;
int	portnum = -1;		    /* Server port number, listening this */
char	*configfile = CONFIGFILE;	/* Server configuration file */

#ifdef KPATH
char    *klinefile = KLINEFILE;         /* Server kline file */

#ifdef DLINES_IN_KPATH
char    *dlinefile = KLINEFILE;
#else
char    *dlinefile = CONFIGFILE;
#endif

#else
char    *klinefile = CONFIGFILE;
char    *dlinefile = CONFIGFILE;
#endif

int	debuglevel = -1;		/* Server debug level */
int	bootopt = 0;			/* Server boot option flags */
char	*debugmode = "";		/*  -"-    -"-   -"-  */
char	*sbrk0;				/* initial sbrk(0) */
static	int	dorehash = 0;
static	char	*dpath = DPATH;
int     rehashed = 1;
int     dline_in_progress = 0;	/* killing off matching D lines ? */
int     noisy_htm=NOISY_HTM;	/* Is high traffic mode noisy or not? */
time_t	nextconnect = 1;	/* time for next try_connections call */
time_t	nextping = 1;		/* same as above for check_pings() */
time_t	nextdnscheck = 0;	/* next time to poll dns to force timeouts */
time_t	nextexpire = 1;		/* next expire run on the dns cache */

#ifndef K_COMMENT_ONLY
extern int is_comment(char *);
#endif

#ifdef	PROFIL
extern	etext();

VOIDSIG	s_monitor()
{
  static	int	mon = 0;
#ifdef	POSIX_SIGNALS
  struct	sigaction act;
#endif

  (void)moncontrol(mon);
  mon = 1 - mon;
#ifdef	POSIX_SIGNALS
  act.sa_handler = s_rehash;
  act.sa_flags = 0;
  (void)sigemptyset(&act.sa_mask);
  (void)sigaddset(&act.sa_mask, SIGUSR1);
  (void)sigaction(SIGUSR1, &act, NULL);
#else
  (void)signal(SIGUSR1, s_monitor);
#endif
}
#endif

VOIDSIG s_die()
{
  flush_connections(me.fd);
#ifdef	USE_SYSLOG
  (void)syslog(LOG_CRIT, "Server killed By SIGTERM");
#endif
  exit(-1);
}

static VOIDSIG s_rehash()
{
#ifdef	POSIX_SIGNALS
  struct	sigaction act;
#endif
  dorehash = 1;
#ifdef	POSIX_SIGNALS
  act.sa_handler = s_rehash;
  act.sa_flags = 0;
  (void)sigemptyset(&act.sa_mask);
  (void)sigaddset(&act.sa_mask, SIGHUP);
  (void)sigaction(SIGHUP, &act, NULL);
#else
  (void)signal(SIGHUP, s_rehash);	/* sysV -argv */
#endif
}

void	restart(char *mesg)
{
  static int was_here = NO; /* redundant due to restarting flag below */

  if(was_here)
    abort();
  was_here = YES;

#ifdef	USE_SYSLOG
  (void)syslog(LOG_WARNING, "Restarting Server because: %s, sbrk(0)-etext: %d",
     mesg,(u_int)sbrk((size_t)0)-(u_int)sbrk0);
#endif
  server_reboot();
}

VOIDSIG s_restart()
{
  static int restarting = 0;

#ifdef	USE_SYSLOG
  (void)syslog(LOG_WARNING, "Server Restarting on SIGINT");
#endif
  if (restarting == 0)
    {
      /* Send (or attempt to) a dying scream to oper if present */

      restarting = 1;
      server_reboot();
    }
}

void	server_reboot()
{
  Reg	int	i;
  
  sendto_ops("Aieeeee!!!  Restarting server... sbrk(0)-etext: %d",
	(u_int)sbrk((size_t)0)-(u_int)sbrk0);

  Debug((DEBUG_NOTICE,"Restarting server..."));
  flush_connections(me.fd);

  /*
  ** fd 0 must be 'preserved' if either the -d or -i options have
  ** been passed to us before restarting.
  */
#ifdef USE_SYSLOG
  (void)closelog();
#endif
  for (i = 3; i < MAXCONNECTIONS; i++)
    (void)close(i);
  if (!(bootopt & (BOOT_TTY|BOOT_DEBUG)))
    (void)close(2);
  (void)close(1);
  if ((bootopt & BOOT_CONSOLE) || isatty(0))
    (void)close(0);
  if (!(bootopt & (BOOT_INETD|BOOT_OPER)))
    (void)execv(MYNAME, myargv);
#ifdef USE_SYSLOG
  /* Have to reopen since it has been closed above */

  openlog(myargv[0], LOG_PID|LOG_NDELAY, LOG_FACILITY);
  syslog(LOG_CRIT, "execv(%s,%s) failed: %m\n", MYNAME, myargv[0]);
  closelog();
#endif
  Debug((DEBUG_FATAL,"Couldn't restart server: %s", strerror(errno)));
  exit(-1);
}


/*
** try_connections
**
**	Scan through configuration and try new connections.
**	Returns the calendar time when the next call to this
**	function should be made latest. (No harm done if this
**	is called earlier or later...)
*/
static	time_t	try_connections(time_t currenttime)
{
  Reg	aConfItem *aconf;
  Reg	aClient *cptr;
  aConfItem **pconf;
  int	connecting, confrq;
  time_t	next = 0;
  aClass	*cltmp;
  aConfItem *con_conf = (aConfItem *)NULL;
  int	con_class = 0;

  connecting = FALSE;
  Debug((DEBUG_NOTICE,"Connection check at   : %s",
	 myctime(currenttime)));
  for (aconf = conf; aconf; aconf = aconf->next )
    {
      /* Also when already connecting! (update holdtimes) --SRB */
      if (!(aconf->status & CONF_CONNECT_SERVER) || aconf->port <= 0)
	continue;
      cltmp = Class(aconf);
      /*
      ** Skip this entry if the use of it is still on hold until
      ** future. Otherwise handle this entry (and set it on hold
      ** until next time). Will reset only hold times, if already
      ** made one successfull connection... [this algorithm is
      ** a bit fuzzy... -- msa >;) ]
      */

      if ((aconf->hold > currenttime))
	{
	  if ((next > aconf->hold) || (next == 0))
	    next = aconf->hold;
	  continue;
	}

      confrq = get_con_freq(cltmp);
      aconf->hold = currenttime + confrq;
      /*
      ** Found a CONNECT config with port specified, scan clients
      ** and see if this server is already connected?
      */
      cptr = find_name(aconf->name, (aClient *)NULL);
      
      if (!cptr && (Links(cltmp) < MaxLinks(cltmp)) &&
	  (!connecting || (Class(cltmp) > con_class)))
	{
	  con_class = Class(cltmp);
	  con_conf = aconf;
	  /* We connect only one at time... */
	  connecting = TRUE;
	}
      if ((next > aconf->hold) || (next == 0))
	next = aconf->hold;
    }
  if (connecting)
    {
      if (con_conf->next)  /* are we already last? */
	{
	  for (pconf = &conf; (aconf = *pconf);
	       pconf = &(aconf->next))
	    /* put the current one at the end and
	     * make sure we try all connections
	     */
	    if (aconf == con_conf)
	      *pconf = aconf->next;
	  (*pconf = con_conf)->next = 0;
	}
      if (connect_server(con_conf, (aClient *)NULL,
			 (struct hostent *)NULL) == 0)
	sendto_ops("Connection to %s[%s] activated.",
		   con_conf->name, con_conf->host);
    }
  Debug((DEBUG_NOTICE,"Next connection check : %s", myctime(next)));
  return (next);
}


/*
 * I re-wrote check_pings a tad
 *
 * check_pings()
 * inputs	- current time
 * output	- next time_t when check_pings() should be called again
 *
 * side effects	- 
 *
 * Clients can be k-lined/d-lined/g-lined/r-lined and exit_client
 * called for each of these.
 *
 * A PING can be sent to clients as necessary.
 *
 * Client/Server ping outs are handled.
 *
 * -Dianora
 */

/* Note, that dying_clients and dying_clients_reason
 * really don't need to be any where near as long as MAXCONNECTIONS
 * but I made it this long for now. If its made shorter,
 * then a limit check is going to have to be added as well
 * -Dianora
 */

aClient *dying_clients[MAXCONNECTIONS];	/* list of dying clients */

#ifdef KLINE_WITH_REASON
char *dying_clients_reason[MAXCONNECTIONS];
#endif

static	time_t	check_pings(time_t currenttime)
{		
  register	aClient	*cptr;		/* current local cptr being examined */
  aConfItem 	*aconf = (aConfItem *)NULL;
  int		ping = 0;		/* ping time value from client */
  int		i;			/* used to index through fd/cptr's */
  time_t	oldest = 0;		/* next ping time */
  time_t	timeout;		/* found necessary ping time */
  char *reason;				/* pointer to reason string */
  int die_index=0;			/* index into list */
					/* of dying clients */
  dying_clients[0] = (aClient *)NULL;	/* mark first one empty */

  /*
   * I re-wrote the way klines are handled. Instead of rescanning
   * the local[] array and calling exit_client() right away, I
   * mark the client thats dying by placing a pointer to its aClient
   * into dying_clients[]. When I have examined all in local[],
   * I then examine the dying_clients[] for aClient's to exit.
   * This saves the rescan on k-lines, also greatly simplifies the code,
   *
   * Jan 28, 1998
   * -Dianora
   */

  for (i = 0; i <= highest_fd; i++)
    {
      if (!(cptr = local[i]) || IsMe(cptr) || IsLog(cptr))
	continue;		/* and go examine next fd/cptr */

      /*
      ** Note: No need to notify opers here. It's
      ** already done when "FLAGS_DEADSOCKET" is set.
      */
      if (cptr->flags & FLAGS_DEADSOCKET)
	{
	  /* N.B. EVERY single time dying_clients[] is set
	   * it must be followed by an immediate continue,
	   * to prevent this cptr from being marked again
	   * this will cause exit_client() to be called twice
	   * for the same cptr. i.e. bad news
	   * -Dianora
	   */

	  dying_clients[die_index++] = cptr;
	  dying_clients[die_index] = (aClient *)NULL;
	  continue;		/* and go examine next fd/cptr */
	}

      /*
       * To make the exit reason have the kline reason
       * I have to keep a copy of the reason in dying_clients_reason[]
       * IF KLINE_WITH_REASON is defined. It is going to be slightly
       * faster if KLINE_WITH_REASON is #undef'ed
       *
       * -Dianora
       */
      
      if (rehashed)
	{
	  if(dline_in_progress)
	    {
	      if(IsPerson(cptr))
		{
		  if( (aconf = find_dkill(cptr)) ) /* if there is a returned 
						      aConfIem then kill it */
		    {
		      sendto_ops("D-line active for %s",
				 get_client_name(cptr, FALSE));
		      reason = aconf->passwd ? aconf->passwd : "D-lined";
		      cptr->flags2 |= FLAGS2_KILLFLAG;
#ifdef KLINE_WITH_REASON
		      dying_clients[die_index] = cptr;
		      dying_clients_reason[die_index++] = reason;
		      dying_clients[die_index] = (aClient *)NULL;
#else
		      dying_clients[die_index++] = cptr;
		      dying_clients[die_index] = (aClient *)NULL;
#endif
		      sendto_one(cptr, err_str(ERR_YOUREBANNEDCREEP),
				 me.name, cptr->name, reason);
		      continue;		/* and go examine next fd/cptr */
		    }
		}
	    }
	  else
	    {
	      if(IsPerson(cptr))
		{
#ifdef GLINES
		  if( (aconf = find_gkill(cptr)) )
		    {
		      sendto_ops("G-line active for %s",
				 get_client_name(cptr, FALSE));
		      reason = aconf->passwd ? aconf->passwd : "G-lined";
		      cptr->flags2 |= FLAGS2_GKILLFLAG;	      
#ifdef KLINE_WITH_REASON
		      dying_clients[die_index] = cptr;
		      dying_clients_reason[die_index++] = reason;
		      dying_clients[die_index] = (aClient *)NULL;
#else
		      dying_clients[die_index++] = cptr;
		      dying_clients[die_index] = (aClient *)NULL;
#endif
		      sendto_one(cptr, err_str(ERR_YOUREBANNEDCREEP),
				 me.name, cptr->name, reason);
		      continue;		/* and go examine next fd/cptr */
		    }
		  else
#endif
		  if((aconf = find_kill(cptr)))	/* if there is a returned
						   aConfItem.. then kill it */
		    {
		      sendto_ops("K-line active for %s",
				 get_client_name(cptr, FALSE));
#ifdef K_COMMENT_ONLY
		      reason = aconf->passwd ? aconf->passwd : "K-lined";
#else
		      reason = (BadPtr(aconf->passwd) || 
				!is_comment(aconf->passwd)) ?
			"K-lined" : aconf->passwd;
#endif
		      cptr->flags2 |= FLAGS2_KILLFLAG;
#ifdef KLINE_WITH_REASON
		      dying_clients[die_index] = cptr;
		      dying_clients_reason[die_index++] = reason;
		      dying_clients[die_index] = (aClient *)NULL;
#else
		      dying_clients[die_index++] = cptr;
		      dying_clients[die_index] = (aClient *)NULL;
#endif
		      sendto_one(cptr, err_str(ERR_YOUREBANNEDCREEP),
				 me.name, cptr->name, reason);
		      continue;		/* and go examine next fd/cptr */
		    }
		}
	    }
	}

#if defined(R_LINES) && defined(R_LINES_OFTEN)
      /*
       * this is used for KILL lines with time restrictions
       * on them - send a message to the user being killed
       * first.
       * *** Moved up above  -taner ***
       *
       * Moved here, no more rflag -Dianora 
       */
      if (IsPerson(cptr) && find_restrict(cptr))
	{
	  sendto_ops("Restricting %s, closing link.",
		     get_client_name(cptr,FALSE));

	  dying_clients[die_index++] = cptr;
	  dying_clients[die_index] = (aClient *)NULL;
	  cptr->flags2 |= FLAGS2_RESTRICTED;
	  continue;			/* and go examine next fd/cptr */
	}
#endif

      if (!IsRegistered(cptr))
	ping = CONNECTTIMEOUT;
      else
	ping = get_client_ping(cptr);

      /*
       * If the server or client hasnt talked to us in 2*ping seconds
       * and it has a ping time, then close its connection.
       */
      if (((currenttime - cptr->lasttime) >= (2 * ping) &&
	   (cptr->flags & FLAGS_PINGSENT)) ||
	  ((!IsRegistered(cptr) && (currenttime - cptr->since) >= ping)) )
	{
	  /* ok. There has been a ping time out. determine what sort
	   *  of ping timeout
	   * Can be one of client ping out, server ping out, or time
	   * out on authd or DNS
	   *
	   * -Dianora
	   */

	  if (!IsRegistered(cptr) &&
	      (DoingDNS(cptr) || DoingAuth(cptr)))
	    {
	      /* Either a DNS or authd failure */

	      if (cptr->authfd >= 0)
		{
		  /* authd failure */

		  (void)close(cptr->authfd);
		  cptr->authfd = -1;
		  cptr->count = 0;
		  *cptr->buffer = '\0';
		}
#ifdef SHOW_HEADERS
	      if (DoingDNS(cptr))
		send(cptr->fd, REPORT_FAIL_DNS, R_fail_dns, 0);
	      else
		send(cptr->fd, REPORT_FAIL_ID, R_fail_id, 0);
#endif
	      Debug((DEBUG_NOTICE,"DNS/AUTH timeout %s",
		     get_client_name(cptr,TRUE)));
	      del_queries((char *)cptr);
	      ClearAuth(cptr);
	      ClearDNS(cptr);
	      SetAccess(cptr);
	      cptr->since = currenttime;
	      continue;			/* and go examine next fd/cptr */
	    }

	  if (IsServer(cptr) || IsConnecting(cptr) ||
	      IsHandshake(cptr))
	    {
	      /* Server ping out */

	      sendto_ops("No response from %s, closing link",
			 get_client_name(cptr, FALSE));
	    }

#ifdef ANTI_IP_SPOOF
	  if(!IsRegistered(cptr) && (cptr->name[0]) && (cptr->user))
	    ircstp->is_ipspoof++;
#endif /* ANTI_IP_SPOOF */

	  dying_clients[die_index++] = cptr;
	  dying_clients[die_index] = (aClient *)NULL;
	  cptr->flags2 |= FLAGS2_PING_TIMEOUT;
	  continue;			/* and go examine next fd/cptr */
	}

      /* I'm allowing a client who isn't registered to PONG
       * and thusly stay connected for at least 100s.
       * If we (being the hybrid team) don't want that
       * then the next if should be:
       * if (IsRegistered(cptr) && ((cptr->flags & FLAGS_PINGSENT) == 0))
       * -Dianora
       */

      if (IsRegistered(cptr) && ((cptr->flags & FLAGS_PINGSENT) == 0))
	{
	  /*
	   * if we havent PINGed the connection and we havent
	   * heard from it in a while, PING it to make sure
	   * it is still alive.
	   */
	  cptr->flags |= FLAGS_PINGSENT;
	  /* not nice but does the job */
	  cptr->lasttime = currenttime - ping;
	  sendto_one(cptr, "PING :%s", me.name);
	}

      if(IsUnknown(cptr))
	{
	  /*
	   * Check UNKNOWN connections - if they have been in this state
	   * for > 100s, close them.
	   */
	  if (cptr->firsttime ? ((timeofday - cptr->firsttime) > 100) : 0)
	    {
	      /* Lets for the time being, make sure this cptr hasn't already 
	       * been marked for exit
	       */

	      if( cptr->flags2 && (FLAGS2_RESTRICTED|
				   FLAGS2_PING_TIMEOUT|
				   FLAGS2_KILLFLAG|
				   FLAGS2_GKILLFLAG|
				   FLAGS2_DKILLFLAG))
		{
		  /* This should never happen /me paranoid -Dianora */
		  sendto_ops("cptr already marked for exit! cptr->flags2 = %x",
			     cptr->flags2);
		}
	      else
		{
		  dying_clients[die_index++] = cptr;
		  dying_clients[die_index] = (aClient *)NULL;
		  cptr->flags2 |= FLAGS2_CONNECTION_TIMEDOUT;
		}
	    }
	}

#ifndef SIMPLE_PINGFREQUENCY
      while (timeout <= currenttime)
	timeout += ping;
      if (timeout < oldest || !oldest)
	oldest = timeout;
#endif

    }

  /* Now exit clients marked for exit above.
   * it doesn't matter if local[] gets re-arranged now
   *
   * -Dianora
   */

  for(die_index = 0; (cptr = dying_clients[die_index]); die_index++)
    {
      if(cptr->flags & FLAGS_DEADSOCKET)
	{
	  (void)exit_client(cptr, cptr, &me, (cptr->flags & FLAGS_SENDQEX) ?
			    "SendQ exceeded" : "Dead socket");
	}
      else if(cptr->flags2 & FLAGS2_KILLFLAG)
	{
#ifdef KLINE_WITH_REASON
	  (void)exit_client(cptr, cptr, &me, dying_clients_reason[die_index]);
#else
	  (void)exit_client(cptr, cptr, &me,"you have been K-lined");
#endif
	}
      else if(cptr->flags2 & FLAGS2_DKILLFLAG)
	{
#ifdef KLINE_WITH_REASON
	  (void)exit_client(cptr, cptr, &me, dying_clients_reason[die_index]);
#else
	  (void)exit_client(cptr, cptr, &me,"you have been D-lined");
#endif
	}
      else if(cptr->flags2 & FLAGS2_GKILLFLAG)
	{
#ifdef KLINE_WITH_REASON
	  (void)exit_client(cptr, cptr, &me, dying_clients_reason[die_index]);
#else
	  (void)exit_client(cptr, cptr, &me,"you have been G-lined");
#endif
	}
      else if(cptr->flags2 & FLAGS2_PING_TIMEOUT)
	{
	  char ping_time_out_buffer[64];
	  (void)sprintf(ping_time_out_buffer,
			"Ping timeout: %i seconds",
			currenttime - cptr->lasttime);
	  (void)exit_client(cptr, cptr, &me,ping_time_out_buffer);
	}
#if defined(R_LINES) && defined(R_LINES_OFTEN)
      else if(cptr->flags2 & FLAGS2_RESTRICTED)
	{
	  (void)exit_client(cptr, cptr, &me,"you have been R-lined");
	}
#endif
      else if(cptr->flags2 & FLAGS2_CONNECTION_TIMEDOUT)
	{
	  (void)exit_client(cptr, cptr, &me, "Connection Timed Out");
	}
    }

  rehashed = 0;
  dline_in_progress = 0;

#ifdef SIMPLE_PINGFREQUENCY
  oldest =  currenttime + PINGFREQUENCY;
#else
  if (!oldest || oldest < currenttime)
    oldest = currenttime + PINGFREQUENCY;
#endif
  Debug((DEBUG_NOTICE,"Next check_ping() call at: %s, %d %d %d",
	 myctime(oldest), ping, oldest, currenttime));
  return (oldest);
}

/*
** bad_command
**	This is called when the commandline is not acceptable.
**	Give error message and exit without starting anything.
*/
static	int	bad_command()
{
  (void)printf(
	 "Usage: ircd %s[-h servername] [-p portnumber] [-x loglevel] [-s] [-t]\n",
#ifdef CMDLINE_CONFIG
	 "[-f config] "
#else
	 ""
#endif
	 );
  (void)printf("Server not started\n\n");
  return (-1);
}
#ifndef TRUE
#define TRUE 1
#endif

/* code added by mika nystrom (mnystrom@mit.edu) */
/* this flag is used to signal globally that the server is heavily loaded,
   something which can be taken into account when processing e.g. user commands
   and scheduling ping checks */
/* Changed by Taner Halicioglu (taner@CERF.NET) */

#define LOADCFREQ 5	/* every 5s */
#define LOADRECV 18	/* 18k/s */

int lifesux = 1;
int LRV = LOADRECV;
time_t LCF = LOADCFREQ;
float currlife = 0.0;

int	main(int argc, char *argv[])
{
  int	portarg = 0;
  uid_t	uid, euid;
  time_t	delay = 0;
  int fd;

  if((timeofday = time(NULL)) == -1)
    {
      (void)fprintf(stderr,"ERROR: Clock Failure (%d)\n", errno);
      exit(errno);
    }

  /*
   * We don't want to calculate these every time they are used :)
   */

  R_do_dns	= strlen(REPORT_DO_DNS);
  R_fin_dns	= strlen(REPORT_FIN_DNS);
  R_fin_dnsc	= strlen(REPORT_FIN_DNSC);
  R_fail_dns	= strlen(REPORT_FAIL_DNS);
  R_do_id		= strlen(REPORT_DO_ID);
  R_fin_id	= strlen(REPORT_FIN_ID);
  R_fail_id	= strlen(REPORT_FAIL_ID);


  Count.server = 1;	/* us */
  Count.oper = 0;
  Count.chan = 0;
  Count.local = 0;
  Count.total = 0;
  Count.invisi = 0;
  Count.unknown = 0;
  Count.max_loc = 0;
  Count.max_tot = 0;

/* this code by mika@cs.caltech.edu */
/* it is intended to keep the ircd from being swapped out. BSD swapping
   criteria do not match the requirements of ircd */
#ifdef SETUID_ROOT
  if(plock(TXTLOCK)<0) fprintf(stderr,"could not plock...\n");
  if(setuid(IRCD_UID)<0)exit(-1); /* blah.. this should be done better */
#endif
#ifdef INITIAL_DBUFS
  dbuf_init();  /* set up some dbuf stuff to control paging */
#endif
  sbrk0 = (char *)sbrk((size_t)0);
  uid = getuid();
  euid = geteuid();
#ifdef	PROFIL
  (void)monstartup(0, etext);
  (void)moncontrol(1);
  (void)signal(SIGUSR1, s_monitor);
#endif
#ifdef	CHROOTDIR
  if (chdir(dpath))
    {
      perror("chdir");
      exit(-1);
    }
  res_init();
  if (chroot(DPATH))
    {
      (void)fprintf(stderr,"ERROR:  Cannot chdir/chroot\n");
      exit(5);
    }
#endif /*CHROOTDIR*/

  myargv = argv;
  (void)umask(077);                /* better safe than sorry --SRB */
  bzero((char *)&me, sizeof(me));

  setup_signals();
  
  /*
  ** All command line parameters have the syntax "-fstring"
  ** or "-f string" (e.g. the space is optional). String may
  ** be empty. Flag characters cannot be concatenated (like
  ** "-fxyz"), it would conflict with the form "-fstring".
  */
  while (--argc > 0 && (*++argv)[0] == '-')
    {
      char	*p = argv[0]+1;
      int	flag = *p++;

      if (flag == '\0' || *p == '\0')
	if (argc > 1 && argv[1][0] != '-')
	  {
	    p = *++argv;
	    argc -= 1;
	  }
	else
	  p = "";
      
      switch (flag)
	{
	case 'a':
	  bootopt |= BOOT_AUTODIE;
	  break;
	case 'c':
	  bootopt |= BOOT_CONSOLE;
	  break;
	case 'q':
	  bootopt |= BOOT_QUICK;
	  break;
	case 'd' :
	  (void)setuid((uid_t)uid);
	  dpath = p;
	  break;
	case 'o': /* Per user local daemon... */
	  (void)setuid((uid_t)uid);
	  bootopt |= BOOT_OPER;
	  break;
#ifdef CMDLINE_CONFIG
	case 'f':
	  (void)setuid((uid_t)uid);
	  configfile = p;
	  break;

#ifdef KPATH
	case 'k':
	  (void)setuid((uid_t)uid);
	  klinefile = p;
	  break;
#endif

#endif
	case 'h':
	  strncpyzt(me.name, p, sizeof(me.name));
	  break;
	case 'i':
	  bootopt |= BOOT_INETD|BOOT_AUTODIE;
	  break;
	case 'p':
	  if ((portarg = atoi(p)) > 0 )
	    portnum = portarg;
	  break;
	case 's':
	  bootopt |= BOOT_STDERR;
	  break;
	case 't':
	  (void)setuid((uid_t)uid);
	  bootopt |= BOOT_TTY;
	  break;
	case 'v':
	  (void)printf("ircd %s\n", version);
	  exit(0);
	case 'x':
#ifdef	DEBUGMODE
	  (void)setuid((uid_t)uid);
	  debuglevel = atoi(p);
	  debugmode = *p ? p : "0";
	  bootopt |= BOOT_DEBUG;
	  break;
#else
	  (void)fprintf(stderr,
			"%s: DEBUGMODE must be defined for -x y\n",
			myargv[0]);
	  exit(0);
#endif
	default:
	  bad_command();
	  break;
	}
    }

#ifndef	CHROOT
  if (chdir(dpath))
    {
      perror("chdir");
      exit(-1);
    }
#endif

#ifndef IRC_UID
  if ((uid != euid) && !euid)
    {
      (void)fprintf(stderr,
		    "ERROR: do not run ircd setuid root. Make it setuid a\
normal user.\n");
      exit(-1);
    }
#endif

#if !defined(CHROOTDIR) || (defined(IRC_UID) && defined(IRC_GID))
# ifndef	AIX
  (void)setuid((uid_t)euid);
# endif

  if ((int)getuid() == 0)
    {
# if defined(IRC_UID) && defined(IRC_GID)

      /* run as a specified user */
      (void)fprintf(stderr,"WARNING: running ircd with uid = %d\n",
		    IRC_UID);
      (void)fprintf(stderr,"         changing to gid %d.\n",IRC_GID);
      (void)setuid(IRC_UID);
      (void)setgid(IRC_GID);
#else
      /* check for setuid root as usual */
      (void)fprintf(stderr,
		    "ERROR: do not run ircd setuid root. Make it setuid a\
 normal user.\n");
      exit(-1);
# endif	
	    } 
#endif /*CHROOTDIR/UID/GID*/

#if 0
  /* didn't set debuglevel */
  /* but asked for debugging output to tty */
  if ((debuglevel < 0) &&  (bootopt & BOOT_TTY))
    {
      (void)fprintf(stderr,
		    "you specified -t without -x. use -x <n>\n");
      exit(-1);
    }
#endif

  if (argc > 0)
    return bad_command(); /* This should exit out */
 
  motd = (aMotd *)NULL;
  helpfile = (aMotd *)NULL;
  motd_tm = NULL;

  read_motd(MOTD);
  read_help(HELPFILE);

  clear_client_hash_table();
  clear_channel_hash_table();
  clear_scache_hash_table();	/* server cache name table */
  clear_ip_hash_table();	/* client host ip hash table */
  initlists();
  initclass();
  initwhowas();
  initstats();
  init_tree_parse(msgtab);
  NOW = time(NULL);
  open_debugfile();
  NOW = time(NULL);
  init_fdlist(&serv_fdlist);
  init_fdlist(&oper_fdlist);
  init_fdlist(&listen_fdlist);

#ifndef NO_PRIORITY
  init_fdlist(&busycli_fdlist);
#endif

  init_fdlist(&default_fdlist);
  {
    register int i;
    for (i=MAXCONNECTIONS+1 ; i>0 ; i--)
      {
	default_fdlist.entry[i] = i-1;
      }
  }
  if((timeofday = time(NULL)) == -1)
    {
      syslog(LOG_WARNING, "Clock Failure (%d), TS can be corrupted", errno);
      sendto_ops("Clock Failure (%d), TS can be corrupted", errno);
    }

  if (portnum < 0)
    portnum = PORTNUM;
  me.port = portnum;
  (void)init_sys();
  me.flags = FLAGS_LISTEN;
  if (bootopt & BOOT_INETD)
    {
      me.fd = 0;
      local[0] = &me;
      me.flags = FLAGS_LISTEN;
    }
  else
    me.fd = -1;

#ifdef USE_SYSLOG
#define SYSLOG_ME     "ircd"
  openlog(SYSLOG_ME, LOG_PID|LOG_NDELAY, LOG_FACILITY);
#endif
  if ((fd = openconf(configfile)) == -1)
    {
      Debug((DEBUG_FATAL, "Failed in reading configuration file %s",
	     configfile));
      (void)printf("Couldn't open configuration file %s\n",
		   configfile);
      exit(-1);
    }
  (void)initconf(bootopt,fd);

/* comstuds SEPARATE_QUOTE_KLINES_BY_DATE code */
#ifdef SEPARATE_QUOTE_KLINES_BY_DATE
  {
    struct tm *tmptr;
    char timebuffer[20], filename[200];

    tmptr = localtime(&NOW);
    (void)strftime(timebuffer, 20, "%y%m%d", tmptr);
    ircsprintf(filename, "%s.%s", klinefile, timebuffer);
    if ((fd = openconf(filename)) == -1)
      {
	Debug((DEBUG_ERROR,"Failed reading kline file %s",
	       filename));
	(void)printf("Couldn't open kline file %s\n",
		     filename);
      }
    (void)initconf(0,fd);
  }
#else
#ifdef KPATH
  if ((fd = openconf(klinefile)) == -1)
    {
      Debug((DEBUG_ERROR,"Failed reading kline file %s",
	     klinefile));
      (void)printf("Couldn't open kline file %s\n",
		   klinefile);
    }
  (void)initconf(0,fd);
#endif
#endif
  if (!(bootopt & BOOT_INETD))
    {
      static	char	star[] = "*";
      aConfItem	*aconf;
      u_long vaddr;
      
      if ((aconf = find_me()) && portarg <= 0 && aconf->port > 0)
	portnum = aconf->port;
      Debug((DEBUG_ERROR, "Port = %d", portnum));
      if ((aconf->passwd[0] != '\0') && (aconf->passwd[0] != '*'))
	vaddr = inet_addr(aconf->passwd);
      else
        vaddr = (u_long) NULL;

      if (inetport(&me, star, portnum, vaddr))
      {
	if (bootopt & BOOT_STDERR)
          fprintf(stderr,"Couldn't bind to primary port %d\n", portnum);
#ifdef USE_SYSLOG
	(void)syslog(LOG_CRIT, "Couldn't bind to primary port %d\n", portnum);
#endif
	exit(1);
      }
    }
  else if (inetport(&me, "*", 0, 0))
  {
    if (bootopt & BOOT_STDERR)
      fprintf(stderr,"Couldn't bind to port passed from inetd\n");
#ifdef USE_SYSLOG
      (void)syslog(LOG_CRIT, "Couldn't bind to port passed from inetd\n");
#endif
    exit(1);
  }

  (void)get_my_name(&me, me.sockhost, sizeof(me.sockhost)-1);
  if (me.name[0] == '\0')
    strncpyzt(me.name, me.sockhost, sizeof(me.name));
  me.hopcount = 0;
  me.authfd = -1;
  me.confs = NULL;
  me.next = NULL;
  me.user = NULL;
  me.from = &me;
  SetMe(&me);
  make_server(&me);
  me.serv->up = me.name;
  me.lasttime = me.since = me.firsttime = NOW;
  (void)add_to_client_hash_table(me.name, &me);

  check_class();
  if (bootopt & BOOT_OPER)
    {
      aClient *tmp = add_connection(&me, 0);
      
      if (!tmp)
	exit(1);
      SetMaster(tmp);
    }
  else
    write_pidfile();

  Debug((DEBUG_NOTICE,"Server ready..."));
#ifdef USE_SYSLOG
  syslog(LOG_NOTICE, "Server Ready");
#endif
  NOW = time(NULL);

#ifndef NO_PRIORITY
  check_fdlists(time(NULL));
#endif

  if((timeofday = time(NULL)) == -1)
    {
      syslog(LOG_WARNING, "Clock Failure (%d), TS can be corrupted", errno);
      sendto_ops("Clock Failure (%d), TS can be corrupted", errno);
    }
  while (1)
    delay = io_loop(delay);
}

time_t io_loop(time_t delay)
{
  static	char	to_send[200];
  static time_t	lasttime	= 0;
  static long	lastrecvK	= 0;
  static int	lrv		= 0;
  time_t lasttimeofday;

  lasttimeofday = timeofday;
  if((timeofday = time(NULL)) == -1)
    {
      syslog(LOG_WARNING, "Clock Failure (%d), TS can be corrupted", errno);
      sendto_ops("Clock Failure (%d), TS can be corrupted", errno);
    }

  if (timeofday < lasttimeofday)
  {
  	(void)ircsprintf(to_send,
		"System clock is running backwards - (%d < %d)",
		timeofday, lasttimeofday);
	report_error(to_send, &me);
  }

  NOW = timeofday;

  /*
   * This chunk of code determines whether or not
   * "life sucks", that is to say if the traffic
   * level is so high that standard server
   * commands should be restricted
   *
   * Changed by Taner so that it tells you what's going on
   * as well as allows forced on (long LCF), etc...
   */
  
  if ((timeofday - lasttime) >= LCF)
    {
      lrv = LRV * LCF;
      lasttime = timeofday;
      currlife = (float)(me.receiveK - lastrecvK)/(float)LCF;
      if ((me.receiveK - lrv) > lastrecvK )
	{
	  if (!lifesux)
	    {
/* In the original +th code Taner had

              LCF << 1;  / * add hysteresis * /

   which does nothing...
   so, the hybrid team changed it to

	      LCF <<= 1;  / * add hysteresis * /

   suddenly, there were reports of clients mysteriously just dropping
   off... Neither rodder or I can see why it makes a difference, but
   lets try it this way...

   The original dog3 code, does not have an LCF variable

   -Dianora

*/
	      lifesux = 1;

	      if(noisy_htm) {
     	        (void)sprintf(to_send, 
                  "Entering high-traffic mode - (%.1fk/s > %dk/s)",
		      (float)currlife, LRV);
	        sendto_ops(to_send);
              }
	    }
	  else
	    {
	      lifesux++;		/* Ok, life really sucks! */
	      LCF += 2;			/* Wait even longer */
              if(noisy_htm) {
	        (void)sprintf(to_send,
		   "Still high-traffic mode %d%s (%d delay): %.1fk/s",
		      lifesux, (lifesux & 0x04) ? " (TURBO)" : "",
		      (int)LCF, (float)currlife);
	        sendto_ops(to_send);
              }
	    }
	}
      else
	{
	  LCF = LOADCFREQ;
	  if (lifesux)
	    {
	      lifesux = 0;
              if(noisy_htm)
	        sendto_ops("Resuming standard operation . . . .");
	    }
	}
      lastrecvK = me.receiveK;
    }

  /*
  ** We only want to connect if a connection is due,
  ** not every time through.  Note, if there are no
  ** active C lines, this call to Tryconnections is
  ** made once only; it will return 0. - avalon
  */
  if (nextconnect && timeofday >= nextconnect)
    nextconnect = try_connections(timeofday);
  /*
  ** DNS checks. One to timeout queries, one for cache expiries.
  */
  if (timeofday >= nextdnscheck)
    nextdnscheck = timeout_query_list(timeofday);
  if (timeofday >= nextexpire)
    nextexpire = expire_cache(timeofday);
  /*
  ** take the smaller of the two 'timed' event times as
  ** the time of next event (stops us being late :) - avalon
  ** WARNING - nextconnect can return 0!
  */
  if (nextconnect)
    delay = MIN(nextping, nextconnect);
  else
    delay = nextping;
  delay = MIN(nextdnscheck, delay);
  delay = MIN(nextexpire, delay);
  delay -= timeofday;
  /*
  ** Adjust delay to something reasonable [ad hoc values]
  ** (one might think something more clever here... --msa)
  ** We don't really need to check that often and as long
  ** as we don't delay too long, everything should be ok.
  ** waiting too long can cause things to timeout...
  ** i.e. PINGS -> a disconnection :(
  ** - avalon
  */
  if (delay < 1)
    delay = 1;
  else
    delay = MIN(delay, TIMESEC);
  /*
   * We want to read servers on every io_loop, as well
   * as "busy" clients (which again, includes servers.
   * If "lifesux", then we read servers AGAIN, and then
   * flush any data to servers.
   *	-Taner
   */

#ifndef NO_PRIORITY
  (void)read_message(0, &serv_fdlist);
  (void)read_message(1, &busycli_fdlist);
  if (lifesux)
    {
      (void)read_message(1, &serv_fdlist);
      if (lifesux & 0x4)
	{	/* life really sucks */
	  (void)read_message(1, &busycli_fdlist);
	  (void)read_message(1, &serv_fdlist);
	}
      (void)flush_fdlist_connections(&serv_fdlist);      
    }
  if((timeofday = time(NULL)) == -1)
    {
      syslog(LOG_WARNING, "Clock Failure (%d), TS can be corrupted", errno);
      sendto_ops("Clock Failure (%d), TS can be corrupted", errno);
    }

  /*
   * CLIENT_SERVER = TRUE:
   * 	If we're in normal mode, or if "lifesux" and a few
   *	seconds have passed, then read everything.
   * CLIENT_SERVER = FALSE:
   *	If it's been more than lifesux*2 seconds (that is, 
   *	at most 1 second, or at least 2s when lifesux is
   *	!= 0) check everything.
   *	-Taner
   */
  {
    static time_t lasttime=0;
#ifdef CLIENT_SERVER
    if (!lifesux || (lasttime + lifesux) < timeofday)
      {
#else
    if ((lasttime + (lifesux + 1)) < timeofday)
      {
#endif
	(void)read_message(delay, NULL); /*  check everything! */
	lasttime = timeofday;
      }
   }
#else
  (void)read_message(delay, NULL); /*  check everything! */
#endif

  /*
  ** ...perhaps should not do these loops every time,
  ** but only if there is some chance of something
  ** happening (but, note that conf->hold times may
  ** be changed elsewhere--so precomputed next event
  ** time might be too far away... (similarly with
  ** ping times) --msa
  */

  if ((timeofday >= nextping))	/* && !lifesux ) */
    nextping = check_pings(timeofday);

  if (dorehash && !lifesux)
    {
      (void)rehash(&me, &me, 1);
      dorehash = 0;
    }
  /*
  ** Flush output buffers on all connections now if they
  ** have data in them (or at least try to flush)
  ** -avalon
  */
  flush_connections(me.fd);

#ifdef	LOCKFILE
  /*
  ** If we have pending klines and
  ** CHECK_PENDING_KLINES minutes
  ** have passed, try writing them
  ** out.  -ThemBones
  */
  if ((pending_klines) && ((timeofday - pending_kline_time)
			   >= (CHECK_PENDING_KLINES * 60)))
    do_pending_klines();
#endif

  return delay;
}

/*
 * open_debugfile
 *
 * If the -t option is not given on the command line when the server is
 * started, all debugging output is sent to the file set by LPATH in config.h
 * Here we just open that file and make sure it is opened to fd 2 so that
 * any fprintf's to stderr also goto the logfile.  If the debuglevel is not
 * set from the command line by -x, use /dev/null as the dummy logfile as long
 * as DEBUGMODE has been defined, else dont waste the fd.
 */
static	void	open_debugfile()
{
#ifdef	DEBUGMODE
  int	fd;
  aClient	*cptr;

  if (debuglevel >= 0)
    {
      cptr = make_client(NULL);
      cptr->fd = 2;
      SetLog(cptr);
      cptr->port = debuglevel;
      cptr->flags = 0;
      cptr->acpt = cptr;
      local[2] = cptr;
      (void)strcpy(cptr->sockhost, me.sockhost);

      (void)printf("isatty = %d ttyname = %#x\n",
		   isatty(2), (u_int)ttyname(2));
      if (!(bootopt & BOOT_TTY)) /* leave debugging output on fd 2 */
	{
	  (void)truncate(LOGFILE, 0);
	  if ((fd = open(LOGFILE, O_WRONLY | O_CREAT, 0600)) < 0) 
	    if ((fd = open("/dev/null", O_WRONLY)) < 0)
	      exit(-1);
	  if (fd != 2)
	    {
	      (void)dup2(fd, 2);
	      (void)close(fd); 
	    }
	  strncpyzt(cptr->name, LOGFILE, sizeof(cptr->name));
	}
      else if (isatty(2) && ttyname(2))
	strncpyzt(cptr->name, ttyname(2), sizeof(cptr->name));
      else
	(void)strcpy(cptr->name, "FD2-Pipe");
      Debug((DEBUG_FATAL, "Debug: File <%s> Level: %d at %s",
	     cptr->name, cptr->port, myctime(time(NULL))));
    }
  else
    local[2] = NULL;
#endif
  return;
}

static	void	setup_signals()
{
#ifdef	POSIX_SIGNALS
  struct	sigaction act;

  act.sa_handler = SIG_IGN;
  act.sa_flags = 0;
  (void)sigemptyset(&act.sa_mask);
  (void)sigaddset(&act.sa_mask, SIGPIPE);
  (void)sigaddset(&act.sa_mask, SIGALRM);
# ifdef	SIGWINCH
  (void)sigaddset(&act.sa_mask, SIGWINCH);
  (void)sigaction(SIGWINCH, &act, NULL);
# endif
  (void)sigaction(SIGPIPE, &act, NULL);
  act.sa_handler = dummy;
  (void)sigaction(SIGALRM, &act, NULL);
  act.sa_handler = s_rehash;
  (void)sigemptyset(&act.sa_mask);
  (void)sigaddset(&act.sa_mask, SIGHUP);
  (void)sigaction(SIGHUP, &act, NULL);
  act.sa_handler = s_restart;
  (void)sigaddset(&act.sa_mask, SIGINT);
  (void)sigaction(SIGINT, &act, NULL);
  act.sa_handler = s_die;
  (void)sigaddset(&act.sa_mask, SIGTERM);
  (void)sigaction(SIGTERM, &act, NULL);

#else
# ifndef	HAVE_RELIABLE_SIGNALS
  (void)signal(SIGPIPE, dummy);
#  ifdef	SIGWINCH
  (void)signal(SIGWINCH, dummy);
#  endif
# else
#  ifdef	SIGWINCH
  (void)signal(SIGWINCH, SIG_IGN);
#  endif
  (void)signal(SIGPIPE, SIG_IGN);
# endif
  (void)signal(SIGALRM, dummy);   
  (void)signal(SIGHUP, s_rehash);
  (void)signal(SIGTERM, s_die); 
  (void)signal(SIGINT, s_restart);
#endif

#ifdef RESTARTING_SYSTEMCALLS
  /*
  ** At least on Apollo sr10.1 it seems continuing system calls
  ** after signal is the default. The following 'siginterrupt'
  ** should change that default to interrupting calls.
  */
  (void)siginterrupt(SIGALRM, 1);
#endif
}

#ifndef NO_PRIORITY
/*
 * This is a pretty expensive routine -- it loops through
 * all the fd's, and finds the active clients (and servers
 * and opers) and places them on the "busy client" list
 */
time_t check_fdlists(now)
time_t now;
{
#ifdef CLIENT_SERVER
#define BUSY_CLIENT(x)	(((x)->priority < 55) || (!lifesux && ((x)->priority < 75)))
#else
#define BUSY_CLIENT(x)	(((x)->priority < 40) || (!lifesux && ((x)->priority < 60)))
#endif
#define FDLISTCHKFREQ  2

  register aClient *cptr;
  register int i,j;

  j = 0;
  for (i=highest_fd; i >=0; i--)
    {
      if (!(cptr=local[i])) continue;
      if (IsServer(cptr) || IsListening(cptr) || IsOper(cptr))
	{
	  busycli_fdlist.entry[++j] = i;
	  continue;
	}
      if (cptr->receiveM == cptr->lastrecvM)
	{
	  cptr->priority += 2;	/* lower a bit */
	  if (cptr->priority > 90) cptr->priority = 90;
	  else if (BUSY_CLIENT(cptr)) busycli_fdlist.entry[++j] = i;
	  continue;
	}
      else
	{
	  cptr->lastrecvM = cptr->receiveM;
	  cptr->priority -= 30;	/* active client */
	  if (cptr->priority < 0)
	    {
	      cptr->priority = 0;
	      busycli_fdlist.entry[++j] = i;
	    }
	  else if (BUSY_CLIENT(cptr)) busycli_fdlist.entry[++j] = i;
	}
    }
  busycli_fdlist.last_entry = j; /* rest of the fdlist is garbage */
  return (now + FDLISTCHKFREQ + (lifesux + 1));
}
#endif

