/************************************************************************
 *   IRC - Internet Relay Chat, src/s_misc.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers.
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
static  char sccsid[] = "@(#)s_misc.c	2.39 27 Oct 1993 (C) 1988 University of Oulu, \
Computing Center and Jarkko Oikarinen";
static char *rcs_version = "$Id: s_misc.c,v 1.29 1998/09/17 14:03:07 db Exp $";
#endif

#include <sys/time.h>
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include <sys/stat.h>
#include <fcntl.h>
#if !defined(ULTRIX) && !defined(SGI) && !defined(sequent) && \
    !defined(__convex__)
# include <sys/param.h>
#endif
#if defined(AIX) || defined(SVR3)
# include <time.h>
#endif
#ifdef HPUX
#include <unistd.h>
#endif
#ifdef DYNIXPTX
#include <sys/types.h>
#include <time.h>
#endif
#include "h.h"
#include "fdlist.h"
/* FDLIST */
extern fdlist serv_fdlist;

#ifdef USE_LINKLIST
/* LINKLIST */
extern aClient *local_cptr_list;
extern aClient *oper_cptr_list;
extern aClient *serv_cptr_list;
#endif

#ifdef NO_CHANOPS_WHEN_SPLIT
extern int server_was_split;
extern time_t server_split_time;
#endif

static	void	exit_one_client (aClient *,aClient *,aClient *,char *);

static	char	*months[] = {
	"January",	"February",	"March",	"April",
	"May",	        "June",	        "July",	        "August",
	"September",	"October",	"November",	"December"
};

static	char	*weekdays[] = {
	"Sunday",	"Monday",	"Tuesday",	"Wednesday",
	"Thursday",	"Friday",	"Saturday"
};

/*
 * stats stuff
 */
struct	stats	ircst, *ircstp = &ircst;

char	*date(time_t clock) 
{
  static	char	buf[80], plus;
  Reg	struct	tm *lt, *gm;
  struct	tm	gmbuf;
  int	minswest;

  if (!clock) 
    time(&clock);
  gm = gmtime(&clock);
  bcopy((char *)gm, (char *)&gmbuf, sizeof(gmbuf));
  gm = &gmbuf;
  lt = localtime(&clock);

  if (lt->tm_yday == gm->tm_yday)
    minswest = (gm->tm_hour - lt->tm_hour) * 60 +
      (gm->tm_min - lt->tm_min);
  else if (lt->tm_yday > gm->tm_yday)
    minswest = (gm->tm_hour - (lt->tm_hour + 24)) * 60;
  else
    minswest = ((gm->tm_hour + 24) - lt->tm_hour) * 60;

  plus = (minswest > 0) ? '-' : '+';
  if (minswest < 0)
    minswest = -minswest;
  
  (void)ircsprintf(buf, "%s %s %d %04d -- %02d:%02d:%02d %c%02d:%02d",
		   weekdays[lt->tm_wday], months[lt->tm_mon],lt->tm_mday,
		   lt->tm_year + 1900, lt->tm_hour, lt->tm_min, lt->tm_sec,
		   plus, minswest/60, minswest%60);

  return buf;
}


/*

*/
char    *smalldate(time_t clock)
{
  static  char    buf[MAX_DATE_STRING];
  Reg     struct  tm *lt, *gm;
  struct  tm      gmbuf;

  if (!clock)
    time(&clock);
  gm = gmtime(&clock);
  bcopy((char *)gm, (char *)&gmbuf, sizeof(gmbuf));
  gm = &gmbuf; 
  lt = localtime(&clock);
  
  (void)ircsprintf(buf, "%04d/%02d/%02d %02d.%02d",
		   lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
		   lt->tm_hour, lt->tm_min);
  
  return buf;
}


#if defined(GLINES) || defined(SEPARATE_QUOTE_KLINES_BY_DATE)
/*
 * small_file_date
 * Make a small YYMMDD formatted string suitable for a
 * dated file stamp. 
 */
char    *small_file_date(time_t clock)
{
  static  char    timebuffer[MAX_DATE_STRING];
  struct tm *tmptr;

  if (!clock)
    time(&clock);
  tmptr = localtime(&clock);
  strftime(timebuffer, MAX_DATE_STRING, "%y%m%d", tmptr);
  return timebuffer;
}
#endif

/**
 ** myctime()
 **   This is like standard ctime()-function, but it zaps away
 **   the newline from the end of that string. Also, it takes
 **   the time value as parameter, instead of pointer to it.
 **   Note that it is necessary to copy the string to alternate
 **   buffer (who knows how ctime() implements it, maybe it statically
 **   has newline there and never 'refreshes' it -- zapping that
 **   might break things in other places...)
 **
 **/

char	*myctime(time_t value)
{
  static	char	buf[28];
  Reg	char	*p;

  (void)strcpy(buf, ctime(&value));
  if ((p = (char *)index(buf, '\n')) != NULL)
    *p = '\0';

  return buf;
}

/*
** check_registered_user is used to cancel message, if the
** originator is a server or not registered yet. In other
** words, passing this test, *MUST* guarantee that the
** sptr->user exists (not checked after this--let there
** be coredumps to catch bugs... this is intentional --msa ;)
**
** There is this nagging feeling... should this NOT_REGISTERED
** error really be sent to remote users? This happening means
** that remote servers have this user registered, although this
** one has it not... Not really users fault... Perhaps this
** error message should be restricted to local clients and some
** other thing generated for remotes...
*/
int	check_registered_user(aClient *sptr)
{
  if (!IsRegisteredUser(sptr))
    {
      sendto_one(sptr, err_str(ERR_NOTREGISTERED), me.name, "*");
      return -1;
    }
  return 0;
}

/*
** check_registered user cancels message, if 'x' is not
** registered (e.g. we don't know yet whether a server
** or user)
*/
int	check_registered(aClient *sptr)
{
  if (!IsRegistered(sptr))
    {
      sendto_one(sptr, err_str(ERR_NOTREGISTERED), me.name, "*");
      return -1;
    }
  return 0;
}

/*
** get_client_name
**      Return the name of the client for various tracking and
**      admin purposes. The main purpose of this function is to
**      return the "socket host" name of the client, if that
**	differs from the advertised name (other than case).
**	But, this can be used to any client structure.
**
**	Returns:
**	  "name[user@ip#.port]" if 'showip' is true;
**	  "name[sockethost]", if name and sockhost are different and
**	  showip is false; else
**	  "name".
**
** NOTE 1:
**	Watch out the allocation of "nbuf", if either sptr->name
**	or sptr->sockhost gets changed into pointers instead of
**	directly allocated within the structure...
**
** NOTE 2:
**	Function return either a pointer to the structure (sptr) or
**	to internal buffer (nbuf). *NEVER* use the returned pointer
**	to modify what it points!!!
*/
char	*get_client_name(aClient *sptr,int showip)
{
  static char nbuf[HOSTLEN * 2 + USERLEN + 5];

  if (MyConnect(sptr))
    {
      switch (showip)
        {
          case TRUE:
#ifdef SHOW_UH
	    (void)ircsprintf(nbuf, "%s[%s%s@%s]",
			     sptr->name,
			     (!(sptr->flags & FLAGS_GOTID)) ? "" :
			     "(+)",
			     sptr->user?sptr->user->username :
			     sptr->username, 
			     inetntoa((char *)&sptr->ip));
#else
	    (void)sprintf(nbuf, "%s[%s@%s]",
			sptr->name,
			(!(sptr->flags & FLAGS_GOTID)) ? "" :
			sptr->username,
			inetntoa((char *)&sptr->ip));
#endif
            break;
          case HIDEME:
#ifdef SHOW_UH
            (void)ircsprintf(nbuf, "%s[%s%s@%s]",
                             sptr->name,
                             (!(sptr->flags & FLAGS_GOTID)) ? "" :
                             "(+)",
                             sptr->user?sptr->user->username :
                             sptr->username,
                             "255.255.255.255");
#else
            (void)sprintf(nbuf, "%s[%s@%s]",
                        sptr->name,
                        (!(sptr->flags & FLAGS_GOTID)) ? "" :
                        sptr->username,
                        "255.255.255.255");
#endif
            break;
	  default:
	      if (mycmp(sptr->name, sptr->sockhost))
#ifdef USERNAMES_IN_TRACE
		(void)ircsprintf(nbuf, "%s[%s@%s]",
				 sptr->name,
				 sptr->user ? sptr->user->username :
				 sptr->username, sptr->sockhost);
#else
	      (void)ircsprintf(nbuf, "%s[%s]",
			       sptr->name, sptr->sockhost);
#endif
	      else
		return sptr->name;
        }
      return nbuf;
    }
  return sptr->name;
}

char  *get_client_host(aClient *cptr)
{
  static char nbuf[HOSTLEN * 2 + USERLEN + 5];
  
  if (!MyConnect(cptr))
    return cptr->name;
  if (!cptr->hostp)
    return get_client_name(cptr, FALSE);
  else
    (void)ircsprintf(nbuf, "%s[%-.*s@%-.*s]",
		     cptr->name, USERLEN,
		     (!(cptr->flags & FLAGS_GOTID)) ? "" : cptr->username,
		     HOSTLEN, cptr->hostp->h_name);
  return nbuf;
 }


/*
 * Form sockhost such that if the host is of form user@host, only the host
 * portion is copied.
 */
void	get_sockhost(aClient *cptr,char *host)
{
  Reg	char	*s;
  if ((s = (char *)index(host, '@')))
    s++;
  else
    s = host;
  strncpyzt(cptr->sockhost, s, sizeof(cptr->sockhost));
}

/*
 * Return wildcard name of my server name according to given config entry
 * --Jto
 */
char	*my_name_for_link(char *name,aConfItem *aconf)
{
  static   char	namebuf[HOSTLEN];
  register int	count = aconf->port;
  register char	*start = name;

  if (count <= 0 || count > 5)
    return start;

  while (count-- && name)
    {
      name++;
      name = (char *)index(name, '.');
    }
  if (!name)
    return start;

  namebuf[0] = '*';
  (void)strncpy(&namebuf[1], name, HOSTLEN - 1);
  namebuf[HOSTLEN - 1] = '\0';
  return namebuf;
}

/*
** exit_client
**	This is old "m_bye". Name  changed, because this is not a
**	protocol function, but a general server utility function.
**
**	This function exits a client of *any* type (user, server, etc)
**	from this server. Also, this generates all necessary prototol
**	messages that this exit may cause.
**
**   1) If the client is a local client, then this implicitly
**	exits all other clients depending on this connection (e.g.
**	remote clients having 'from'-field that points to this.
**
**   2) If the client is a remote client, then only this is exited.
**
** For convenience, this function returns a suitable value for
** m_function return value:
**
**	FLUSH_BUFFER	if (cptr == sptr)
**	0		if (cptr != sptr)
*/
int	exit_client(
aClient *cptr,	/*
		** The local client originating the exit or NULL, if this
		** exit is generated by this server for internal reasons.
		** This will not get any of the generated messages.

		*/
aClient *sptr,	/* Client exiting */
aClient *from,	/* Client firing off this Exit, never NULL! */
char	*comment	/* Reason for the exit */
                   )
{
  Reg	aClient	*acptr;
  Reg	aClient	*next;
#ifdef	FNAME_USERLOG
  time_t	on_for;
#endif
  char	comment1[HOSTLEN + HOSTLEN + 2];
  if (MyConnect(sptr))
    {
#ifdef LIMIT_UH
      if(sptr->flags & FLAGS_IPHASH)
        remove_one_ip(sptr);
#else
      if(sptr->flags & FLAGS_IPHASH)
        remove_one_ip(sptr->ip.s_addr);
#endif
      if (IsAnOper(sptr))
	{
          /* FDLIST */
	  delfrom_fdlist(sptr->fd, &oper_fdlist);

#ifdef USE_LINKLIST
          /* LINKLIST */
          /* oh for in-line functions... */
          {
            aClient *prev_cptr=(aClient *)NULL;
            aClient *cur_cptr = oper_cptr_list;
            while(cur_cptr) 
              {
                if(sptr == cur_cptr)
                  {
                    if(prev_cptr)
                      prev_cptr->next_oper_client = cur_cptr->next_oper_client;
                    else
                      oper_cptr_list = cur_cptr->next_oper_client;
		    cur_cptr->next_oper_client = (aClient *)NULL;
                    break;
                  }
		else
		  prev_cptr = cur_cptr;
                cur_cptr = cur_cptr->next_oper_client;
              }
          }
#endif
	}
      if (IsClient(sptr))
        {
          Count.local--;

#ifdef USE_LINKLIST
          /* LINKLIST */
          /* oh for in-line functions... */
          if(IsPerson(sptr))	/* a little extra paranoia */
            {
              aClient *prev_cptr = (aClient *)NULL;
              aClient *cur_cptr = local_cptr_list;
              while(cur_cptr)
                {
                  if(sptr == cur_cptr)
                    {
                      if(prev_cptr)
                        prev_cptr->next_local_client = 
			  cur_cptr->next_local_client;
                      else
                        local_cptr_list = cur_cptr->next_local_client;
                      cur_cptr->next_local_client = (aClient *)NULL;
		      break;
                    }
		  else
		    prev_cptr = cur_cptr;
                  cur_cptr = cur_cptr->next_local_client;
                }
            }
#endif
        }
      if (IsServer(sptr))
	{
	  Count.myserver--;
          /* FDLIST */
	  delfrom_fdlist(sptr->fd, &serv_fdlist);

#ifdef USE_LINKLIST
          /* LINKLIST */
          /* oh for in-line functions... */
          {
            aClient *prev_cptr = (aClient *)NULL;
            aClient *cur_cptr = serv_cptr_list;
            while(cur_cptr)
              {
                if(sptr == cur_cptr)
                  {
                    if(prev_cptr)
                      prev_cptr->next_server_client =
			cur_cptr->next_server_client;
                    else
                      serv_cptr_list = cur_cptr->next_server_client;
                    cur_cptr->next_server_client = (aClient *)NULL;
                    break;
                  }
		else
		  prev_cptr = cur_cptr;
                cur_cptr = cur_cptr->next_server_client;
	      }
          }
#endif

#ifdef NO_CHANOPS_WHEN_SPLIT
	  /*	  if(serv_fdlist.entry[1] <= serv_fdlist.last_entry) */
	  if(Count.myserver == 0)
	    {
	      server_was_split = YES;
	      server_split_time = NOW;
	    }
#endif
	}
      sptr->flags |= FLAGS_CLOSING;
      if (IsPerson(sptr))
	{
	  sendto_realops_lev(CCONN_LEV, "Client exiting: %s (%s@%s) [%s] [%s]",
		    sptr->name, sptr->user->username,
		    sptr->user->host,
		    (sptr->flags & FLAGS_NORMALEX) ?
		    "Client Quit" : comment,
		    sptr->hostip);
	}
#ifdef FNAME_USERLOG
	  on_for = timeofday - sptr->firsttime;
# if defined(USE_SYSLOG) && defined(SYSLOG_USERS)
	  if (IsPerson(sptr))
	    syslog(LOG_NOTICE, "%s (%3d:%02d:%02d): %s!%s@%s %d/%d\n",
		   myctime(sptr->firsttime),
		   on_for / 3600, (on_for % 3600)/60,
		   on_for % 60, sptr->name,
		   sptr->user->username, sptr->user->host,
		   sptr->sendK, sptr->receiveK);
# else
	  {
	    char	linebuf[300];
	    static int	logfile = -1;
	    static long	lasttime;

	    /*
	     * This conditional makes the logfile active only after
	     * it's been created - thus logging can be turned off by
	     * removing the file.
	     *
	     * stop NFS hangs...most systems should be able to open a
	     * file in 3 seconds. -avalon (curtesy of wumpus)
	     *
	     * Keep the logfile open, syncing it every 10 seconds
	     * -Taner
	     */
	    if (IsPerson(sptr))
	      {
		if (logfile == -1)
		  {
		    /*		    (void)alarm(3); */
		    logfile = open(FNAME_USERLOG, O_WRONLY|O_APPEND);
		    /* (void)alarm(0); */
		  }
		(void)ircsprintf(linebuf,
				 "%s (%3d:%02d:%02d): %s!%s@%s %d/%d\n",
				 myctime(sptr->firsttime), on_for / 3600,
				 (on_for % 3600)/60, on_for % 60,
				 sptr->name,
				 sptr->user->username,
				 sptr->user->host,
				 sptr->sendK,
				 sptr->receiveK);
		/*		(void)alarm(3); */
		(void)write(logfile, linebuf, strlen(linebuf));
		/*		(void)alarm(0); */
		/*
		 * Resync the file evey 10 seconds
		 */
		if (timeofday - lasttime > 10)
		  {
		    (void)alarm(3);
		    (void)close(logfile);
		    /*		    (void)alarm(0); */
		    logfile = -1;
		    lasttime = timeofday;
		  }
	      }
	  }
# endif
#endif
	  if (sptr->fd >= 0)
	    {
	      if (cptr != NULL && sptr != cptr)
		sendto_one(sptr, "ERROR :Closing Link: %s %s (%s)",
			   sptr->sockhost,
			   sptr->name, comment);
	      else
		sendto_one(sptr, "ERROR :Closing Link: %s (%s)",
			   sptr->sockhost,
			   comment);
	    }
	  /*
	  ** Currently only server connections can have
	  ** depending remote clients here, but it does no
	  ** harm to check for all local clients. In
	  ** future some other clients than servers might
	  ** have remotes too...
	  **
	  ** Close the Client connection first and mark it
	  ** so that no messages are attempted to send to it.
	  ** (The following *must* make MyConnect(sptr) == FALSE!).
	  ** It also makes sptr->from == NULL, thus it's unnecessary
	  ** to test whether "sptr != acptr" in the following loops.
	  */
	  if(IsServer(sptr))
	    {	
	      sendto_ops("%s was connected for %lu seconds.  %lu/%lu sendK/recvK.",
			 sptr->name, timeofday - sptr->firsttime,
			 sptr->sendK, sptr->receiveK);
#ifdef USE_SYSLOG
	      syslog(LOG_NOTICE, "%s was connected for %lu seconds.  %lu/%lu sendK/recvK.", sptr->name, timeofday - sptr->firsttime, sptr->sendK, sptr->receiveK);
#endif
	      close_connection(sptr);
	      /*
	      ** First QUIT all NON-servers which are behind this link
	      **
	      ** Note	There is no danger of 'cptr' being exited in
	      **	the following loops. 'cptr' is a *local* client,
	      **	all dependants are *remote* clients.
	      */
	      
	      /* This next bit is a a bit ugly but all it does is take the
	      ** name of us.. me.name and tack it together with the name of
	      ** the server sptr->name that just broke off and puts this
	      ** together into exit_one_client() to provide some useful
	      ** information about where the net is broken.      Ian 
	      */
	      (void)strcpy(comment1, me.name);
	      (void)strcat(comment1," ");
	      (void)strcat(comment1, sptr->name);
	      for (acptr = client; acptr; acptr = next)
		{
		  next = acptr->next;
		  if (!IsServer(acptr) && acptr->from == sptr)
		    exit_one_client(NULL, acptr, &me, comment1);
		}
	      /*
	      ** Second SQUIT all servers behind this link
	      */
	      for (acptr = client; acptr; acptr = next)
		{
		  next = acptr->next;
		  if (IsServer(acptr) && acptr->from == sptr)
		    exit_one_client(NULL, acptr, &me, me.name);
		}
	    }
	  else
	    close_connection(sptr);

	}
      exit_one_client(cptr, sptr, from, comment);
      return cptr == sptr ? FLUSH_BUFFER : 0;
}

/*
** Exit one client, local or remote. Assuming all dependants have
** been already removed, and socket closed for local client.
*/
static	void	exit_one_client(aClient *cptr,
				aClient *sptr,
				aClient *from,
				char *comment)
{
  Reg	aClient *acptr;
  Reg	int	i;
  Reg	Link	*lp;

  /*
  **  For a server or user quitting, propogate the information to
  **  other servers (except to the one where is came from (cptr))
  */
  if (IsMe(sptr))
    {
      sendto_ops("ERROR: tried to exit me! : %s", comment);
      return;	/* ...must *never* exit self!! */
    }
  else if (IsServer(sptr))
    {
      /*
      ** Old sendto_serv_but_one() call removed because we now
      ** need to send different names to different servers
      ** (domain name matching)
      */
      for (i = 0; i <= highest_fd; i++)
	{
	  Reg	aConfItem *aconf;
	  
	  if (!(acptr = local[i]) || !IsServer(acptr) ||
	      acptr == cptr || IsMe(acptr))
	    continue;
	  if ((aconf = acptr->serv->nline) &&
	      (matches(my_name_for_link(me.name, aconf),
		       sptr->name) == 0))
	    continue;
	  /*
	  ** SQUIT going "upstream". This is the remote
	  ** squit still hunting for the target. Use prefixed
	  ** form. "from" will be either the oper that issued
	  ** the squit or some server along the path that
	  ** didn't have this fix installed. --msa
	  */
	  if (sptr->from == acptr)
	    {
	      sendto_one(acptr, ":%s SQUIT %s :%s",
			 from->name, sptr->name, comment);
	    }
	  else
	    {
	      sendto_one(acptr, "SQUIT %s :%s",
			 sptr->name, comment);
	    }
	}
    } else if (!(IsPerson(sptr)))
      /* ...this test is *dubious*, would need
      ** some thought.. but for now it plugs a
      ** nasty hole in the server... --msa
      */
      ; /* Nothing */
  else if (sptr->name[0]) /* ...just clean all others with QUIT... */
    {
      /*
      ** If this exit is generated from "m_kill", then there
      ** is no sense in sending the QUIT--KILL's have been
      ** sent instead.
      */
      if ((sptr->flags & FLAGS_KILLED) == 0)
	{
	  sendto_serv_butone(cptr,":%s QUIT :%s",
			     sptr->name, comment);
	}
      /*
      ** If a person is on a channel, send a QUIT notice
      ** to every client (person) on the same channel (so
      ** that the client can show the "**signoff" message).
      ** (Note: The notice is to the local clients *only*)
      */
      if (sptr->user)
	{
	  sendto_common_channels(sptr, ":%s QUIT :%s",
				   sptr->name, comment);

	  while ((lp = sptr->user->channel))
	    remove_user_from_channel(sptr,lp->value.chptr);
	  
	  /* Clean up invitefield */
	  while ((lp = sptr->user->invited))
	    del_invite(sptr, lp->value.chptr);
	  /* again, this is all that is needed */
	}
    }
  
  /* Remove sptr from the client list */
  if (del_from_client_hash_table(sptr->name, sptr) != 1)
    {
/*
 * This is really odd - oh well, it just generates noise... -Taner
 *
 *      sendto_realops("%#x !in tab %s[%s]", sptr, sptr->name,
 *	     sptr->from ? sptr->from->sockhost : "??host");
 *      sendto_realops("from = %#x", sptr->from);
 *      sendto_realops("next = %#x", sptr->next);
 *      sendto_realops("prev = %#x", sptr->prev);
 *      sendto_realops("fd = %d  status = %d", sptr->fd, sptr->status);
 *      sendto_realops("user = %#x", sptr->user);
 */

      Debug((DEBUG_ERROR, "%#x !in tab %s[%s] %#x %#x %#x %d %d %#x",
	     sptr, sptr->name,
	     sptr->from ? sptr->from->sockhost : "??host",
	     sptr->from, sptr->next, sptr->prev, sptr->fd,
	     sptr->status, sptr->user));
    }
  remove_client_from_list(sptr);
  return;
}

void	checklist()
{
  Reg	aClient	*acptr;
  Reg	int	i,j;

  if (!(bootopt & BOOT_AUTODIE))
    return;
  for (j = i = 0; i <= highest_fd; i++)
    if (!(acptr = local[i]))
      continue;
    else if (IsClient(acptr))
      j++;
  if (!j)
    {
#ifdef	USE_SYSLOG
      syslog(LOG_WARNING,"ircd exiting: checklist() s_misc.c autodie");
#endif
      exit(0);
    }
  return;
}

void	initstats()
{
  bzero((char *)&ircst, sizeof(ircst));
}

void	tstats(aClient *cptr,char *name)
{
  Reg	aClient	*acptr;
  Reg	int	i;
  Reg	struct stats	*sp;
  struct	stats	tmp;

  sp = &tmp;
  bcopy((char *)ircstp, (char *)sp, sizeof(*sp));
  for (i = 0; i < highest_fd; i++)
    {
      if (!(acptr = local[i]))
	continue;
      if (IsServer(acptr))
	{
	  sp->is_sbs += acptr->sendB;
	  sp->is_sbr += acptr->receiveB;
	  sp->is_sks += acptr->sendK;
	  sp->is_skr += acptr->receiveK;
	  sp->is_sti += timeofday - acptr->firsttime;
	  sp->is_sv++;
	  if (sp->is_sbs > 1023)
	    {
	      sp->is_sks += (sp->is_sbs >> 10);
	      sp->is_sbs &= 0x3ff;
	    }
	  if (sp->is_sbr > 1023)
	    {
	      sp->is_skr += (sp->is_sbr >> 10);
	      sp->is_sbr &= 0x3ff;
	    }
	  
	}
      else if (IsClient(acptr))
	{
	  sp->is_cbs += acptr->sendB;
	  sp->is_cbr += acptr->receiveB;
	  sp->is_cks += acptr->sendK;
	  sp->is_ckr += acptr->receiveK;
	  sp->is_cti += timeofday - acptr->firsttime;
	  sp->is_cl++;
	  if (sp->is_cbs > 1023)
	    {
	      sp->is_cks += (sp->is_cbs >> 10);
	      sp->is_cbs &= 0x3ff;
	    }
	  if (sp->is_cbr > 1023)
	    {
	      sp->is_ckr += (sp->is_cbr >> 10);
	      sp->is_cbr &= 0x3ff;
	    }
	  
	}
      else if (IsUnknown(acptr))
	sp->is_ni++;
    }

  sendto_one(cptr, ":%s %d %s :accepts %u refused %u",
	     me.name, RPL_STATSDEBUG, name, sp->is_ac, sp->is_ref);
  sendto_one(cptr, ":%s %d %s :unknown commands %u prefixes %u",
	     me.name, RPL_STATSDEBUG, name, sp->is_unco, sp->is_unpf);
  sendto_one(cptr, ":%s %d %s :nick collisions %u unknown closes %u",
	     me.name, RPL_STATSDEBUG, name, sp->is_kill, sp->is_ni);
  sendto_one(cptr, ":%s %d %s :wrong direction %u empty %u",
	     me.name, RPL_STATSDEBUG, name, sp->is_wrdi, sp->is_empt);
  sendto_one(cptr, ":%s %d %s :numerics seen %u mode fakes %u",
	     me.name, RPL_STATSDEBUG, name, sp->is_num, sp->is_fake);
  sendto_one(cptr, ":%s %d %s :auth successes %u fails %u",
	     me.name, RPL_STATSDEBUG, name, sp->is_asuc, sp->is_abad);
  sendto_one(cptr, ":%s %d %s :local connections %u udp packets %u",
	     me.name, RPL_STATSDEBUG, name, sp->is_loc, sp->is_udp);
  sendto_one(cptr, ":%s %d %s :Client Server",
	     me.name, RPL_STATSDEBUG, name);
  sendto_one(cptr, ":%s %d %s :connected %u %u",
	     me.name, RPL_STATSDEBUG, name, sp->is_cl, sp->is_sv);
  sendto_one(cptr, ":%s %d %s :bytes sent %u.%uK %u.%uK",
	     me.name, RPL_STATSDEBUG, name,
	     sp->is_cks, sp->is_cbs, sp->is_sks, sp->is_sbs);
  sendto_one(cptr, ":%s %d %s :bytes recv %u.%uK %u.%uK",
	     me.name, RPL_STATSDEBUG, name,
	     sp->is_ckr, sp->is_cbr, sp->is_skr, sp->is_sbr);
  sendto_one(cptr, ":%s %d %s :time connected %u %u",
	     me.name, RPL_STATSDEBUG, name, sp->is_cti, sp->is_sti);
#ifdef FLUD
  sendto_one(cptr, ":%s %d %s :CTCP Floods Blocked %u",
             me.name, RPL_STATSDEBUG, name, sp->is_flud);
#endif /* FLUD */
#ifdef ANTI_IP_SPOOF
  sendto_one(cptr, ":%s %d %s :IP Spoofers %u",
             me.name, RPL_STATSDEBUG, name, sp->is_ipspoof);
#endif /* ANTI_IP_SPOOF */
}


/*
 * Retarded - so sue me :-P
 */
#define	_1MEG	(1024.0)
#define	_1GIG	(1024.0*1024.0)
#define	_1TER	(1024.0*1024.0*1024.0)
#define	_GMKs(x)	( (x > _1TER) ? "Terabytes" : ((x > _1GIG) ? "Gigabytes" : \
			((x > _1MEG) ? "Megabytes" : "Kilobytes")))
#define	_GMKv(x)	( (x > _1TER) ? (float)(x/_1TER) : ((x > _1GIG) ? \
			(float)(x/_1GIG) : ((x > _1MEG) ? (float)(x/_1MEG) : (float)x)))

void serv_info(aClient *cptr,char *name)
{
  static char Lformat[] = ":%s %d %s %s %u %u %u %u %u :%u %u %s";
  int	j, fd;
  long	sendK, receiveK, uptime;
  aClient	*acptr;
#ifndef USE_LINKLIST
  fdlist	l;

  l = serv_fdlist;

  sendK = receiveK = 0;

  for (fd = l.entry[j=1]; j <= l.last_entry; fd = l.entry[++j])
    {
      if (!(acptr = local[fd]))
	continue;
      sendK += acptr->sendK;
      receiveK += acptr->receiveK;
      sendto_one(cptr, Lformat, me.name, RPL_STATSLINKINFO,
		 name, get_client_name(acptr, TRUE),
		 (int)DBufLength(&acptr->sendQ),
		 (int)acptr->sendM, (int)acptr->sendK,
		 (int)acptr->receiveM, (int)acptr->receiveK,
		 timeofday - acptr->firsttime,
		 timeofday - acptr->since,
		 IsServer(acptr) ? (DoesTS(acptr) ?
				    "TS" : "NoTS") : "-");
    }
#else

  sendK = receiveK = 0;
  j = 1;

  for(acptr = serv_cptr_list; acptr; acptr = acptr->next_server_client)
    {
      sendK += acptr->sendK;
      receiveK += acptr->receiveK;
      sendto_one(cptr, Lformat, me.name, RPL_STATSLINKINFO,
		 name, get_client_name(acptr, TRUE),
		 (int)DBufLength(&acptr->sendQ),
		 (int)acptr->sendM, (int)acptr->sendK,
		 (int)acptr->receiveM, (int)acptr->receiveK,
		 timeofday - acptr->firsttime,
		 timeofday - acptr->since,
		 IsServer(acptr) ? (DoesTS(acptr) ?
				    "TS" : "NoTS") : "-");
      j++;
    }

#endif

  sendto_one(cptr, ":%s %d %s :%u total server%s",
	     me.name, RPL_STATSDEBUG, name, --j, (j==1)?"":"s");

  sendto_one(cptr, ":%s %d %s :Sent total : %7.2f %s",
	     me.name, RPL_STATSDEBUG, name, _GMKv(sendK), _GMKs(sendK));
  sendto_one(cptr, ":%s %d %s :Recv total : %7.2f %s",
	     me.name, RPL_STATSDEBUG, name, _GMKv(receiveK), _GMKs(receiveK));

  uptime = (timeofday - me.since);
  sendto_one(cptr, ":%s %d %s :Server send: %7.2f %s (%4.1f K/s)",
	     me.name, RPL_STATSDEBUG, name, _GMKv(me.sendK), _GMKs(me.sendK),
	     (float)((float)me.sendK / (float)uptime));
  sendto_one(cptr, ":%s %d %s :Server recv: %7.2f %s (%4.1f K/s)",
	     me.name, RPL_STATSDEBUG, name, _GMKv(me.receiveK), _GMKs(me.receiveK),
	     (float)((float)me.receiveK / (float)uptime));
}

/*
 * show_opers
 * show who is opered on this server
 */

void show_opers(aClient *cptr,char *name)
{
  register aClient	*cptr2;
  register int j=0;
#ifndef	USE_LINKLIST
  register int i, fd;
  fdlist *l;
  
  l = &oper_fdlist;
  for (fd = l->entry[i=1]; i <= l->last_entry; fd = l->entry[++i])
    {
      if (!(cptr2 = local[fd]))
	continue;
      if (!IsClient(cptr2))
	{
	  sendto_one(cptr, ":%s %d %s :Weird fd %d(%d) - not client (%d)",
		     me.name, RPL_STATSDEBUG, name, fd, cptr2->fd,
		     cptr2->status);
	  continue;
	}
      j++;
      sendto_one(cptr, ":%s %d %s :%s (%s@%s) Idle: %d",
		 me.name, RPL_STATSDEBUG, name, cptr2->name,
		 cptr2->user->username, cptr2->user->host,
		 timeofday - cptr2->user->last);
    }
#else	/* USE_LINKLIST */

  for(cptr2 = oper_cptr_list; cptr2; cptr2 = cptr2->next_oper_client)
    {
      j++;
      sendto_one(cptr, ":%s %d %s :%s (%s@%s) Idle: %d",
		 me.name, RPL_STATSDEBUG, name, cptr2->name,
		 cptr2->user->username, cptr2->user->host,
		 timeofday - cptr2->user->last);
    }

#endif
  sendto_one(cptr, ":%s %d %s :%d OPER%s", me.name, RPL_STATSDEBUG,
	     name, j, (j==1) ? "" : "s");
}

void show_servers(aClient *cptr,char *name)
{
  Reg aClient	*cptr2;
  register j=0;
#ifndef	USE_LINKLIST
  register int i, fd;
  fdlist  *l;
 
  l = &serv_fdlist;
  for (fd = l->entry[i=1]; i <= l->last_entry; fd = l->entry[++i])
    {
      if (!(cptr2 = local[fd]))
	continue;
      if (!IsServer(cptr2))
	{
	  sendto_one(cptr, ":%s %d %s :Weird fd %d(%d) - not server (%d)",
		     me.name, RPL_STATSDEBUG, name, fd, cptr2->fd,
		     cptr2->status);
	  continue;
	}
      j++;
      sendto_one(cptr, ":%s %d %s :%s (%s!%s@%s) Idle: %d",
		 me.name, RPL_STATSDEBUG, name, cptr2->name,
		 (cptr2->serv->by[0] ? cptr2->serv->by : "Remote."), 
		 (cptr2->serv->user ? cptr2->serv->user->username : "*"),
		 (cptr2->serv->user ? cptr2->serv->user->host : "*"),
		 timeofday - cptr2->lasttime);
    }
#else /* USE_LINKLIST */
  for(cptr2 = serv_cptr_list; cptr2; cptr2 = cptr2->next_server_client)
    {
      j++;
      sendto_one(cptr, ":%s %d %s :%s (%s!%s@%s) Idle: %d",
		 me.name, RPL_STATSDEBUG, name, cptr2->name,
		 (cptr2->serv->by[0] ? cptr2->serv->by : "Remote."), 
		 (cptr2->serv->user ? cptr2->serv->user->username : "*"),
		 (cptr2->serv->user ? cptr2->serv->user->host : "*"),
		 timeofday - cptr2->lasttime);
    }
#endif

  sendto_one(cptr, ":%s %d %s :%d Server%s", me.name, RPL_STATSDEBUG,
	     name, j, (j==1) ? "" : "s");
}


