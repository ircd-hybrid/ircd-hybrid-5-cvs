/************************************************************************
 *   IRC - Internet Relay Chat, src/s_conf.c
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
static  char sccsid[] = "@(#)s_conf.c	2.56 02 Apr 1994 (C) 1988 University of Oulu, \
Computing Center and Jarkko Oikarinen";

static char *rcs_version = "$Id: s_conf.c,v 1.37 1998/09/17 14:03:04 db Exp $";
#endif

#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "inet.h"
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/wait.h>
#ifdef __hpux
#include "inet.h"
#endif
#if defined(AIX) || defined(DYNIXPTX) || defined(SVR3)
#include <time.h>
#endif
#ifdef	R_LINES
#include <signal.h>
#endif

#include <signal.h>
#include "h.h"
extern int rehashed;
#include "dich_conf.h"

#ifdef	TIMED_KLINES
static	int	check_time_interval(char *);
#endif

struct sockaddr_in vserv;
char	specific_virtual_host;

/* internally defined functions */

static	int	lookup_confhost (aConfItem *);
static  int     attach_iline(aClient *, aConfItem *);
aConfItem *temporary_klines = (aConfItem *)NULL;
static	int	find_port_in_use(aConfItem *);

static  char *set_conf_flags(aConfItem *,char *);
static  int  get_oper_privs(int,char *);

/* externally defined functions */
extern  void    outofmemory(void);	/* defined in list.c */

/* usually, with hash tables, you use a prime number...
   but in this case I am dealing with ip addresses, not ascii strings.
*/

#define IP_HASH_SIZE 0x1000
#define DLINE_HASH_SIZE 0x1000

typedef struct ip_entry
{
  unsigned long ip;
  int	count;
  struct ip_entry *next;
#ifdef LIMIT_UH
  Link  *ptr_clients_on_this_ip;
  int	count_of_idented_users_on_this_ip;
#endif
}IP_ENTRY;

typedef struct d_line_entry
{
  unsigned long ip;
  unsigned long mask;
  aConfItem *conf_entry;
  struct d_line_entry *next;
}DLINE_ENTRY;

IP_ENTRY *ip_hash_table[IP_HASH_SIZE];
DLINE_ENTRY *dline_hash_table[DLINE_HASH_SIZE];

static void clear_dlines(void);		/* clear current d lines */

static int hash_ip(unsigned long);
static int hash_dline_ip(unsigned long);

#ifdef LIMIT_UH
static IP_ENTRY *find_or_add_ip(aClient *);
#else
static IP_ENTRY *find_or_add_ip(unsigned long);
#endif

/* externally defined routines */
int find_conf_match(aClient *,aConfList *,aConfList *,aConfList *);
extern	void  delist_conf(aConfItem *);

#ifdef GLINES
static  aConfItem *glines = (aConfItem *)NULL;
void expire_pending_glines();	/* defined here */
int majority_gline(aClient *,
			  char *,char *,
			  char *,char *,
			  char *,char *,char *); /* defined here */
void add_gline(aConfItem *);
extern void log_gline(aClient *,char *,GLINE_PENDING *,
		      char *,char *,char *,char *,char *,
		      char *,char *);	/* defined in s_serv.c */
static GLINE_PENDING *pending_glines;
#endif

/* general conf items link list root */
aConfItem	*conf = ((aConfItem *)NULL);
/* i line link list root */
aConfItem	*iconf = ((aConfItem *)NULL);

#ifdef LOCKFILE 
extern void do_pending_klines(void);
#endif

/*
 * remove all conf entries from the client except those which match
 * the status field mask.
 */
void	det_confs_butmask(aClient *cptr,int mask)
{
  Reg Link *tmp, *tmp2;

  for (tmp = cptr->confs; tmp; tmp = tmp2)
    {
      tmp2 = tmp->next;
      if ((tmp->value.aconf->status & mask) == 0)
	(void)detach_conf(cptr, tmp->value.aconf);
    }
}

/*
 * Match address by #IP bitmask (10.11.12.128/27)
 * (from ircd2.9.5)
 */
int    match_ipmask(mask, cptr)
char   *mask;
aClient *cptr;
{
        int i1, i2, i3, i4, m;
        u_long lmask, baseip;
 
        if (sscanf(mask, "%d.%d.%d.%d/%d", &i1, &i2, &i3, &i4, &m) != 5 ||
           m < 1 || m > 31) {
               sendto_ops("Ignoring bad mask: %s", mask);
                return -1;
        }
        lmask = htonl(0xfffffffful << (32 - m)); /* /24 -> 0xffffff00ul */
        baseip = htonl(i1 * 0x1000000 + i2 * 0x10000 + i3 * 0x100 + i4);
        return ((cptr->ip.s_addr & lmask) == baseip) ? 0 : 1;
}


/*
 * find the first (best) I line to attach.
 */
/*
  cleanup aug 3 1997 - Dianora
 */

int	attach_Iline(aClient *cptr,
		     struct hostent *hp,
		     char *sockhost)
{
  Reg	aConfItem	*aconf;
  Reg	char	*hname;
  Reg	int	i;
  static	char	uhost[HOSTLEN+USERLEN+3];
  static	char	uhost2[HOSTLEN+USERLEN+3];
  static	char	fullname[HOSTLEN+1];

  *uhost = '\0';
  *uhost2 = '\0';

  for (aconf = conf; aconf; aconf = aconf->next)
    {
      if (aconf->status != CONF_CLIENT)
	continue;

      if (aconf->port && aconf->port != cptr->acpt->port)
	continue;

      if (!aconf->host || !aconf->name)
	{
	  return(attach_iline(cptr,aconf));
	}

      if (hp)
	for (i = 0, hname = hp->h_name; hname;
	     hname = hp->h_aliases[i++])
	  {
	    (void)strncpy(fullname, hname,
			  sizeof(fullname)-1);
	    add_local_domain(fullname,
			     HOSTLEN - strlen(fullname));
	    Debug((DEBUG_DNS, "a_il: %s->%s",
		   sockhost, fullname));
	    if (index(aconf->name, '@'))
	      {
		(void)strcpy(uhost, cptr->username);
		(void)strcat(uhost, "@");
		(void)strcpy(uhost2, cptr->username);
		(void)strcat(uhost2, "@");
	      }
	    else
	      {
		*uhost = '\0';
		*uhost2 = '\0';
	      }

	    (void)strncat(uhost, fullname,
			  sizeof(uhost) - strlen(uhost));
	    (void)strncat(uhost2, sockhost,
			  sizeof(uhost2) - strlen(uhost2));

	    if (index(uhost, '@'))
	      cptr->flags |= FLAGS_DOID;
	    get_sockhost(cptr, uhost);
	    if ((!match(aconf->name, uhost)) ||
		(!match(aconf->name, uhost2)))
	      return(attach_iline(cptr,aconf));
	  }

      if (index(aconf->host, '@'))
	{	
	  /* strncpyzt(uhost, username, sizeof(uhost));
	     username is limited in length -Sol */
	  strncpyzt(uhost, cptr->username, USERLEN+1);
	  (void)strcat(uhost, "@");
	}
      else
	*uhost = '\0';
      (void)strncat(uhost, sockhost, sizeof(uhost) - strlen(uhost));

      if (index(uhost, '@'))
	cptr->flags |= FLAGS_DOID;
      get_sockhost(cptr, uhost);

      /* ircd 2.9.5 ip mask matching */
      if (strchr(aconf->host, '/'))		/* 1.2.3.0/24 */
	{
	  if (match_ipmask(aconf->host, cptr) == 0)
	    return(attach_iline(cptr,aconf));
	}
      else if (match(aconf->host, uhost) == 0)	/* 1.2.3.* */
	return(attach_iline(cptr,aconf));
    }

  return -1;	/* -1 on no match *bleh* */
}

/*
 *  rewrote to remove the "ONE" lamity *BLEH* I agree with comstud
 *  on this one. 
 * - Dianora
 */

static int attach_iline(
		 aClient *cptr,
		 aConfItem *aconf)
{
  IP_ENTRY *ip_found;

/* every conf when created, has a class pointer set up. 
   if it isn't, well.  *BOOM* ! */

  /* if LIMIT_UH is set, limit clients by idented usernames not by ip */

#ifdef LIMIT_UH
  ip_found = find_or_add_ip(cptr);
  cptr->flags |= FLAGS_IPHASH;
  ip_found->count++;
#else
  ip_found = find_or_add_ip(cptr->ip.s_addr);
  cptr->flags |= FLAGS_IPHASH;
  ip_found->count++;
#endif

#ifdef LIMIT_UH
  if ((aconf->class->conFreq) && (ip_found->count_of_idented_users_on_this_ip
				  > aconf->class->conFreq))
    return -4; /* Already at maximum allowed idented users@host */
#else
  /* only check it if its non zero */
  if ((aconf->class->conFreq) && (ip_found->count > aconf->class->conFreq))
    return -4; /* Already at maximum allowed ip#'s */
#endif

  if(IsLimitIp(aconf) && (ip_found->count > 1))
    return -4; /* Already at maximum allowed ip#'s */
  
  return ( attach_conf(cptr, aconf) );
}

/* link list of free IP_ENTRY's */

static IP_ENTRY *free_ip_entries;

/*
 * clear_ip_hash_table()
 *
 * input		- NONE
 * output		- NONE
 * side effects	- clear the ip hash table
 *
 * stole the link list pre-allocator from list.c
*/

void clear_ip_hash_table()
{
  void *block_IP_ENTRIES;	/* block of IP_ENTRY's */
  IP_ENTRY *new_IP_ENTRY;	/* new IP_ENTRY being made */
  IP_ENTRY *last_IP_ENTRY;	/* last IP_ENTRY in chain */
  int size;
  int n_left_to_allocate = MAXCONNECTIONS;

  /* ok. if the sizeof the struct isn't aligned with that of the
     smallest guaranteed valid pointer (void *), then align it
     ya. you could just turn 'size' into a #define. do it. :-)

     -Dianora
     */

  size = sizeof(IP_ENTRY) + (sizeof(IP_ENTRY) & (sizeof(void*) - 1) );

  block_IP_ENTRIES = (void *)MyMalloc((size * n_left_to_allocate));  

  free_ip_entries = (IP_ENTRY *)block_IP_ENTRIES;
  last_IP_ENTRY = free_ip_entries;

  /* *shudder* pointer arithmetic */
  while(--n_left_to_allocate)
    {
      block_IP_ENTRIES = (void *)((unsigned long)block_IP_ENTRIES + 
			(unsigned long) size);
      new_IP_ENTRY = (IP_ENTRY *)block_IP_ENTRIES;
      last_IP_ENTRY->next = new_IP_ENTRY;
      new_IP_ENTRY->next = (IP_ENTRY *)NULL;
      last_IP_ENTRY = new_IP_ENTRY;
    }
  bzero((char *)ip_hash_table, sizeof(ip_hash_table));
}

/* 
 * find_or_add_ip()
 *
 * ifdef LIMIT_UH
 * inputs		- cptr
 * else
 * inputs		- unsigned long IP address value
 * endif
 *
 * output		- pointer to an IP_ENTRY element
 * side effects	-
 *
 * If the ip # was not found, a new IP_ENTRY is created, and the ip
 * count set to 0.
 */

#ifdef LIMIT_UH
static IP_ENTRY *find_or_add_ip(aClient *cptr)
#else
static IP_ENTRY *find_or_add_ip(unsigned long ip_in)
#endif
{
#ifdef LIMIT_UH
  unsigned long ip_in=cptr->ip.s_addr;	
  Link *old_link;
  Link *new_link;
#endif

  int hash_index;
  IP_ENTRY *ptr, *newptr;

  newptr = (IP_ENTRY *)NULL;
  ptr = ip_hash_table[hash_index = hash_ip(ip_in)];
  while(ptr)
    {
      if(ptr->ip == ip_in)
	{
#ifdef LIMIT_UH
	  new_link = make_link();
	  new_link->value.cptr = cptr;
	  new_link->next = ptr->ptr_clients_on_this_ip;
	  ptr->ptr_clients_on_this_ip = new_link;
	  ptr->count_of_idented_users_on_this_ip =
	    count_users_on_this_ip(ptr,cptr);
#endif
	  return(ptr);
	}
      else
	ptr = ptr->next;
    }

  if ( (ptr = ip_hash_table[hash_index]) != (IP_ENTRY *)NULL )
    {
      if( free_ip_entries == (IP_ENTRY *)NULL)
	{
	  sendto_ops("s_conf.c free_ip_entries was found NULL in find_or_add");
	  sendto_ops("Please report to the hybrid team! ircd-hybrid@vol.com");
	  outofmemory();
	}

      newptr = ip_hash_table[hash_index] = free_ip_entries;
      free_ip_entries = newptr->next;

      newptr->ip = ip_in;
      newptr->count = 0;
      newptr->next = ptr;
#ifdef LIMIT_UH
      newptr->count_of_idented_users_on_this_ip = 0;
      new_link = make_link();
      new_link->value.cptr = cptr;
      new_link->next = (Link *)NULL;
      newptr->ptr_clients_on_this_ip = new_link;
#endif
      return(newptr);
    }
  else
    {
      if( free_ip_entries == (IP_ENTRY *)NULL)
        {
          sendto_ops("s_conf.c free_ip_entries was found NULL in find_or_add");
          sendto_ops("Please report to the hybrid team! ircd-hybrid@vol.com");
          outofmemory();
        }

      ptr = ip_hash_table[hash_index] = free_ip_entries;
      free_ip_entries = ptr->next;
      ptr->ip = ip_in;
      ptr->count = 0;
      ptr->next = (IP_ENTRY *)NULL;
#ifdef LIMIT_UH
      ptr->count_of_idented_users_on_this_ip = 0;
      new_link = make_link();
      new_link->value.cptr = cptr;
      new_link->next = (Link *)NULL;
      ptr->ptr_clients_on_this_ip = new_link;
#endif
     return (ptr);
    }
}

#ifdef LIMIT_UH
int count_users_on_this_ip(IP_ENTRY *ip_list,aClient *this_client)
{
  char *my_user_name;
  int count=0;
  Link *ptr;

  if( (ptr = ip_list->ptr_clients_on_this_ip) == (Link *)NULL)
    return(0);
  
  if(this_client->user)
    {
      my_user_name = this_client->user->username;
      if(*my_user_name == '~')
	my_user_name++;
    }
  else
    {
      /* nasty shouldn't happen, but this is better than coring */
      sendto_ops("s_conf.c count_users_on_this_ip my_user_name->user NULL");
      sendto_ops("Please report to the hybrid team! ircd-hybrid@vol.com");
      return 0;
    }

  while(ptr)
    {
      if(ptr->value.cptr->user)
	{
	  if(ptr->value.cptr->user->username[0] == '~')
	    {
	      if(!strcasecmp(ptr->value.cptr->user->username+1,my_user_name))
		  count++;
	    }
	  else
	    {
	      if(!strcasecmp(ptr->value.cptr->user->username,my_user_name))
		  count++;
	    }
	}
      ptr = ptr->next;
    }
  return(count);
}
#endif

/* 
 * remove_one_ip
 *
 * inputs	- unsigned long IP address value
 * output	- NONE
 * side effects	- ip address listed, is looked up in ip hash table
 *		  and number of ip#'s for that ip decremented.
 *		  if ip # count reaches 0, the IP_ENTRY is returned
 *		  to the free_ip_enties link list.
 */

#ifdef LIMIT_UH
void remove_one_ip(aClient *cptr)
#else
void remove_one_ip(unsigned long ip_in)
#endif
{
  int hash_index;
  IP_ENTRY *last_ptr;
  IP_ENTRY *ptr;
  IP_ENTRY *old_free_ip_entries;
#ifdef LIMIT_UH
  unsigned long ip_in=cptr->ip.s_addr;
  Link *prev_link;
  Link *cur_link;
#endif

  last_ptr = ptr = ip_hash_table[hash_index = hash_ip(ip_in)];
  while(ptr)
    {
      if(ptr->ip == ip_in)
	{
          if(ptr->count != 0)
            ptr->count--;
#ifdef LIMIT_UH

	  /* remove the corresponding pointer to this cptr as well */
	  prev_link = (Link *)NULL;
	  cur_link = ptr->ptr_clients_on_this_ip;

	  while(cur_link)
	    {
	      if(cur_link->value.cptr == cptr)
		{
		  if(prev_link)
		    prev_link->next = cur_link->next;
		  else
		    ptr->ptr_clients_on_this_ip = cur_link->next;
		  free_link(cur_link);
		  break;
		}
	      else
		prev_link = cur_link;
	      cur_link = cur_link->next;
	    }
#endif
	  if(ptr->count == 0)
	    {
              if(ip_hash_table[hash_index] == ptr)
                ip_hash_table[hash_index] = ptr->next;
	      else
		last_ptr->next = ptr->next;
	
              if(free_ip_entries != (IP_ENTRY *)NULL)
                {
                  old_free_ip_entries = free_ip_entries;
                  free_ip_entries = ptr;
                  ptr->next = old_free_ip_entries;
                }
              else
                {
                  free_ip_entries = ptr;
                  ptr->next = (IP_ENTRY *)NULL;
                }
	    }
	  return;
	}
      else
        {
          last_ptr = ptr;
	  ptr = ptr->next;
        }
    }
  sendto_ops("s_conf.c couldn't find ip# in hash table in remove_one_ip()");
  sendto_ops("Please report to the hybrid team! ircd-hybrid@vol.com");
  return;
}

/*
 * hash_ip()
 * 
 * input	- unsigned long ip address
 * output	- integer value used as index into hash table
 * side effects	- hopefully, none
 */


static int hash_ip(unsigned long ip)
{
  int hash;
  ip = ntohl(ip);
  hash = ((ip >>= 12) + ip) & (IP_HASH_SIZE-1);
  return(hash);
}

/* Added so s_debug could check memory usage in here -Dianora */
/*
 * count_ip_hash
 *
 * inputs	- pointer to counter of number of ips hashed 
 *		- pointer to memory used for ip hash
 * output	- returned via pointers input
 * side effects	- NONE
 *
 * number of hashed ip #'s is counted up, plus the amount of memory
 * used in the hash.
 */

void count_ip_hash(int *number_ips_stored,u_long *mem_ips_stored)
{
  IP_ENTRY *ip_hash_ptr;
  int i;

  *number_ips_stored = 0;
  *mem_ips_stored = 0;

  for(i = 0; i < IP_HASH_SIZE ;i++)
    {
      ip_hash_ptr = ip_hash_table[i];
      while(ip_hash_ptr)
        {
          *number_ips_stored = *number_ips_stored + 1;
          *mem_ips_stored = *mem_ips_stored +
             sizeof(IP_ENTRY);

          ip_hash_ptr = ip_hash_ptr->next;
        }
    }
}

/*
 * count_dline_hash
 *
 * inputs	- pointer to counter of number of dlines hashed 
 *		- pointer to memory used for dline hash
 * output	- returned via pointers input
 * side effects	- NONE
 *
 * number of dlined #'s is counted up, plus the amount of memory
 * used in the hash.
 */

void count_dline_hash(int *number_dlines_stored,u_long *mem_dlines_stored)
{
  DLINE_ENTRY *dline_hash_ptr;
  int i;

  *number_dlines_stored = 0;
  *mem_dlines_stored = 0;

  for(i = 0; i < DLINE_HASH_SIZE ;i++)
    {
      dline_hash_ptr = dline_hash_table[i];
      while(dline_hash_ptr)
        {
          *number_dlines_stored = *number_dlines_stored + 1;
          *mem_dlines_stored = *mem_dlines_stored +
             sizeof(DLINE_ENTRY);

          dline_hash_ptr = dline_hash_ptr->next;
        }
    }
}

/*
 * iphash_stats()
 *
 * inputs	- 
 * output	-
 * side effects	-
 */

void iphash_stats(aClient *cptr, aClient *sptr,int parc, char *parv[],int out)
{
  IP_ENTRY *ip_hash_ptr;
  int i;
  int collision_count;
  char result_buf[256];

  if(out < 0)
    sendto_one(sptr,":%s NOTICE %s :*** hash stats for iphash",
	       me.name,cptr->name);
  else
    {
      (void)sprintf(result_buf,"*** hash stats for iphash\n");
      (void)write(out,result_buf,strlen(result_buf));
    }

  for(i = 0; i < IP_HASH_SIZE ;i++)
    {
      ip_hash_ptr = ip_hash_table[i];

      collision_count = 0;
      while(ip_hash_ptr)
	{
          collision_count++;
	  ip_hash_ptr = ip_hash_ptr->next;
	}
      if(collision_count)
	{
	  if(out < 0)
	    {
	      sendto_one(sptr,":%s NOTICE %s :Entry %d (0x%X) Collisions %d",
			 me.name,cptr->name,i,i,collision_count);
	    }
	  else
	    {
	      (void)sprintf(result_buf,"Entry %d (0x%X) Collisions %d\n",
			    i,i,collision_count);
	      (void)write(out,result_buf,strlen(result_buf));
	    }
	}
    }
}

/*
 * Find the single N line and return pointer to it (from list).
 * If more than one then return NULL pointer.
 */
aConfItem	*count_cnlines(Link *lp)
{
  Reg	aConfItem	*aconf, *cline = NULL, *nline = NULL;
  
  for (; lp; lp = lp->next)
    {
      aconf = lp->value.aconf;
      if (!(aconf->status & CONF_SERVER_MASK))
	continue;
      if (aconf->status == CONF_CONNECT_SERVER && !cline)
	cline = aconf;
      else if (aconf->status == CONF_NOCONNECT_SERVER && !nline)
	nline = aconf;
    }
  return nline;
}

/*
** detach_conf
**	Disassociate configuration from the client.
**      Also removes a class from the list if marked for deleting.
*/
int	detach_conf(aClient *cptr,aConfItem *aconf)
{
  Reg	Link	**lp, *tmp;

  lp = &(cptr->confs);

  while (*lp)
    {
      if ((*lp)->value.aconf == aconf)
	{
	  if ((aconf) && (Class(aconf)))
	    {
	      if (aconf->status & CONF_CLIENT_MASK)
		if (ConfLinks(aconf) > 0)
		  --ConfLinks(aconf);
	      if (ConfMaxLinks(aconf) == -1 &&
		  ConfLinks(aconf) == 0)
		{
		  free_class(Class(aconf));
		  Class(aconf) = NULL;
		}
	    }
	  if (aconf && !--aconf->clients && IsIllegal(aconf))
	    free_conf(aconf);
	  tmp = *lp;
	  *lp = tmp->next;
	  free_link(tmp);
	  return 0;
	}
      else
	lp = &((*lp)->next);
    }
  return -1;
}

static	int	is_attached(aConfItem *aconf,aClient *cptr)
{
  Reg	Link	*lp;

  for (lp = cptr->confs; lp; lp = lp->next)
    if (lp->value.aconf == aconf)
      break;
  
  return (lp) ? 1 : 0;
}

/*
** attach_conf
**	Associate a specific configuration entry to a *local*
**	client (this is the one which used in accepting the
**	connection). Note, that this automatically changes the
**	attachment if there was an old one...
*/
int	attach_conf(aClient *cptr,aConfItem *aconf)
{
  Reg Link *lp;

  if (is_attached(aconf, cptr))
    {
      return 1;
    }
  if (IsIllegal(aconf))
    {
      return -1;
    }
  /*
   * By using "ConfLinks(aconf) >= ConfMaxLinks(aconf)....
   * the client limit is set by the Y line, connection class, not
   * by the individual client count in each I line conf.
   *
   * -Dianora
   *
   */

  /* If the requested change, is to turn them into an OPER, then
   * they are already attached to a fd there is no need to check for
   * max in a class now is there?
   *
   * -Dianora
   */

  /* If OLD_Y_LIMIT is defined the code goes back to the old way
   * I lines used to work, i.e. number of clients per I line
   * not total in Y
   * -Dianora
   */
#ifdef OLD_Y_LIMIT
  if ((aconf->status & (CONF_LOCOP | CONF_OPERATOR | CONF_CLIENT)) &&
    aconf->clients >= ConfMaxLinks(aconf) && ConfMaxLinks(aconf) > 0)
#else
  if ( (aconf->status & (CONF_LOCOP | CONF_OPERATOR ) ) == 0 )
    {
      if ((aconf->status & CONF_CLIENT) &&
	  ConfLinks(aconf) >= ConfMaxLinks(aconf) && ConfMaxLinks(aconf) > 0)
#endif
	{
	  if (!IsConfFlined(aconf))
	    {
	      return -3;	/* Use this for printing error message */
	    }
	  else
	    {
	      send(cptr->fd,
		   "NOTICE FLINE :I: line is full, but you have an F: line!\n",
		   56, 0);
	      SetFlined(cptr);
	    }

	}
#ifndef OLD_Y_LIMIT
    }
#endif

  lp = make_link();
  lp->next = cptr->confs;
  lp->value.aconf = aconf;
  cptr->confs = lp;
  aconf->clients++;
  if (aconf->status & CONF_CLIENT_MASK)
    ConfLinks(aconf)++;
  return 0;
}


aConfItem *find_admin()
{
  Reg aConfItem *aconf;

  for (aconf = conf; aconf; aconf = aconf->next)
    if (aconf->status & CONF_ADMIN)
      break;
  
  return (aconf);
}

aConfItem *find_me()
{
  Reg aConfItem *aconf;
  for (aconf = conf; aconf; aconf = aconf->next)
    if (aconf->status & CONF_ME)
      return(aconf);

#ifdef USE_SYSLOG
  syslog(LOG_CRIT,"Server has no M: line");
#endif

  exit(-1);

  return((aConfItem *)NULL);	/* oh oh... is there code to handle
				   this case ? - Dianora */
				/* There is now... -Dianora */
}

/*
 * attach_confs
 *  Attach a CONF line to a client if the name passed matches that for
 * the conf file (for non-C/N lines) or is an exact match (C/N lines
 * only).  The difference in behaviour is to stop C:*::* and N:*::*.
 */
aConfItem *attach_confs(aClient *cptr,char *name,int statmask)
{
  Reg aConfItem *tmp;
  aConfItem *first = NULL;
  int len = strlen(name);
  
  if (!name || len > HOSTLEN)
    return((aConfItem *)NULL);

  for (tmp = conf; tmp; tmp = tmp->next)
    {
      if ((tmp->status & statmask) && !IsIllegal(tmp) &&
	  ((tmp->status & (CONF_SERVER_MASK|CONF_HUB)) == 0) &&
	  tmp->name && !match(tmp->name, name))
	{
	  if (!attach_conf(cptr, tmp) && !first)
	    first = tmp;
	}
      else if ((tmp->status & statmask) && !IsIllegal(tmp) &&
	       (tmp->status & (CONF_SERVER_MASK|CONF_HUB)) &&
	       tmp->name && !mycmp(tmp->name, name))
	{
	  if (!attach_conf(cptr, tmp) && !first)
	    first = tmp;
	}
    }
  return (first);
}

/*
 * Added for new access check    meLazy
 */
aConfItem *attach_confs_host(aClient *cptr,char *host,int statmask)
{
  Reg	aConfItem *tmp;
  aConfItem *first = NULL;
  int	len = strlen(host);
  
  if (!host || len > HOSTLEN)
    return( (aConfItem *)NULL);

  for (tmp = conf; tmp; tmp = tmp->next)
    {
      if ((tmp->status & statmask) && !IsIllegal(tmp) &&
	  (tmp->status & CONF_SERVER_MASK) == 0 &&
	  (!tmp->host || match(tmp->host, host) == 0))
	{
	  if (!attach_conf(cptr, tmp) && !first)
	    first = tmp;
	}
      else if ((tmp->status & statmask) && !IsIllegal(tmp) &&
	       (tmp->status & CONF_SERVER_MASK) &&
	       (tmp->host && mycmp(tmp->host, host) == 0))
	{
	  if (!attach_conf(cptr, tmp) && !first)
	    first = tmp;
	}
    }
  return (first);
}

/*
 * find a conf entry which matches the hostname and has the same name.
 */
aConfItem *find_conf_exact(char *name,
			   char *user,
			   char *host,
			   int statmask)
{
  Reg	aConfItem *tmp;
  char	userhost[USERLEN+HOSTLEN+3];

  (void)ircsprintf(userhost, "%s@%s", user, host);

  for (tmp = conf; tmp; tmp = tmp->next)
    {
      if (!(tmp->status & statmask) || !tmp->name || !tmp->host ||
	  mycmp(tmp->name, name))
	continue;
      /*
      ** Accept if the *real* hostname (usually sockecthost)
      ** socket host) matches *either* host or name field
      ** of the configuration.
      */
      if (match(tmp->host, userhost))
	continue;
      if (tmp->status & (CONF_OPERATOR|CONF_LOCOP))
	{
	  if (tmp->clients < MaxLinks(Class(tmp)))
	    return tmp;
	  else
	    continue;
	}
      else
	return tmp;
    }
  return((aConfItem *)NULL);
}

/*
find_conf_name()


** Accept if the *real* hostname (usually sockecthost)
** matches *either* host or name field of the configuration.
*/

aConfItem *find_conf_name(char *name,int statmask)
{
  Reg	aConfItem *tmp;
 
  for (tmp = conf; tmp; tmp = tmp->next)
    {
      if ((tmp->status & statmask) &&
	  (!tmp->name || match(tmp->name, name) == 0))
	return tmp;
    }
  return((aConfItem *)NULL);
}

aConfItem *find_conf(Link *lp,char *name,int statmask)
{
  Reg	aConfItem *tmp;
  int	namelen = name ? strlen(name) : 0;
  
  if (namelen > HOSTLEN)
    return ((aConfItem *) NULL);

  for (; lp; lp = lp->next)
    {
      tmp = lp->value.aconf;
      if ((tmp->status & statmask) &&
	  (((tmp->status & (CONF_SERVER_MASK|CONF_HUB)) &&
	    tmp->name && !mycmp(tmp->name, name)) ||
	   ((tmp->status & (CONF_SERVER_MASK|CONF_HUB)) == 0 &&
	    tmp->name && !match(tmp->name, name))))
	return tmp;
    }
  return((aConfItem *) NULL);
}

/*
 * Added for new access check    meLazy
 */
aConfItem *find_conf_host(Link *lp, char *host,int statmask)
{
  Reg	aConfItem *tmp;
  int	hostlen = host ? strlen(host) : 0;
  
  if (hostlen > HOSTLEN || BadPtr(host))
    return ((aConfItem *)NULL);
  for (; lp; lp = lp->next)
    {
      tmp = lp->value.aconf;
      if (tmp->status & statmask &&
	  (!(tmp->status & CONF_SERVER_MASK || tmp->host) ||
	   (tmp->host && !match(tmp->host, host))))
	return tmp;
    }
  return ((aConfItem *)NULL);
}

/*
 * find_conf_ip
 *
 * Find a conf line using the IP# stored in it to search upon.
 * Added 1/8/92 by Avalon.
 */
aConfItem *find_conf_ip(Link *lp,char *ip,char *user, int statmask)
{
  Reg	aConfItem *tmp;
  Reg	char	*s;
  
  for (; lp; lp = lp->next)
    {
      tmp = lp->value.aconf;
      if (!(tmp->status & statmask))
	continue;
      s = index(tmp->host, '@');
      if(s == (char *)NULL)
	continue;
      *s = '\0';
      if (match(tmp->host, user))
	{
	  *s = '@';
	  continue;
	}
      *s = '@';
      if (!bcmp((char *)&tmp->ipnum, ip, sizeof(struct in_addr)))
	return tmp;
    }
  return ((aConfItem *)NULL);
}

/*
 * find_conf_entry
 *
 * - looks for a match on all given fields.
 */
aConfItem *find_conf_entry(aConfItem *aconf, int mask)
{
  Reg	aConfItem *bconf;

  for (bconf = conf, mask &= ~CONF_ILLEGAL; bconf; bconf = bconf->next)
    {
      if (!(bconf->status & mask) || (bconf->port != aconf->port))
	continue;
      
      if ((BadPtr(bconf->host) && !BadPtr(aconf->host)) ||
	  (BadPtr(aconf->host) && !BadPtr(bconf->host)))
	continue;
      if (!BadPtr(bconf->host) && mycmp(bconf->host, aconf->host))
	continue;

      if ((BadPtr(bconf->passwd) && !BadPtr(aconf->passwd)) ||
	  (BadPtr(aconf->passwd) && !BadPtr(bconf->passwd)))
	continue;
      if (!BadPtr(bconf->passwd) &&
	  mycmp(bconf->passwd, aconf->passwd))
      continue;

	  if ((BadPtr(bconf->name) && !BadPtr(aconf->name)) ||
	      (BadPtr(aconf->name) && !BadPtr(bconf->name)))
	  continue;
	  if (!BadPtr(bconf->name) && mycmp(bconf->name, aconf->name))
	  continue;
	  break;
	  }
      return bconf;
}

/*
 * partially reconstruct an ircd.conf file (tsk tsk, you should have
 * been making backups;but we've all done it)
 * I just cull out the N/C/O/o/A lines, you'll have to finish
 * the rest to use this dump.
 *
 * -Dianora
 */

int	rehash_dump(aClient *sptr,char *parv0)
{
  register aConfItem *aconf;
  int out;
  char ircd_dump_file[256];
  char result_buf[256];
  char timebuffer[MAX_DATE_STRING];
  struct tm *tmptr;

  tmptr = localtime(&NOW);
  strftime(timebuffer, MAX_DATE_STRING, "%y%m%d%H%M", tmptr);
  (void)sprintf(ircd_dump_file,"%s/ircd.conf.%s",
		DPATH,timebuffer);
  
  if ((out = open(ircd_dump_file, O_RDWR|O_APPEND|O_CREAT,0664))==-1)
    {
      sendto_one(sptr, ":%s NOTICE %s :Problem opening %s ",
		 me.name, parv0, ircd_dump_file);
      return -1;
    }
  else
    sendto_one(sptr, ":%s NOTICE %s :Dump ircd.conf to %s ",
	       me.name, parv0, ircd_dump_file);

  for(aconf=conf; aconf; aconf = aconf->next)
    {
      aClass *class_ptr;
      class_ptr = Class(aconf);
      if(aconf->status == CONF_CONNECT_SERVER)
	{
	  
	  (void)sprintf(result_buf,"C:%s:%s:%s::%d\n",
			aconf->host,aconf->passwd,aconf->name,
			class_ptr->class);
	  (void)write(out,result_buf,strlen(result_buf));
	}
      else if(aconf->status == CONF_NOCONNECT_SERVER)
	{
	  (void)sprintf(result_buf,"N:%s:%s:%s::%d\n",
			aconf->host,aconf->passwd,aconf->name,
			class_ptr->class);
	  (void)write(out,result_buf,strlen(result_buf));
	}
      else if(aconf->status == CONF_OPERATOR)
	{
	  (void)sprintf(result_buf,"O:%s:%s:%s::%d\n",
			aconf->host,aconf->passwd,aconf->name,
			class_ptr->class);
	  (void)write(out,result_buf,strlen(result_buf));
	}
      else if(aconf->status == CONF_LOCOP)
	{
	  (void)sprintf(result_buf,"o:%s:%s:%s::%d\n",
			aconf->host,aconf->passwd,aconf->name,
			class_ptr->class);
	  (void)write(out,result_buf,strlen(result_buf));
	}
      else if(aconf->status == CONF_ADMIN)
	{
	  (void)sprintf(result_buf,"A:%s:%s:%s::\n",
			aconf->host,aconf->passwd,aconf->name);
	  (void)write(out,result_buf,strlen(result_buf));
	}
    }
  (void)close(out);
  return 0;
}

/*
 * rehash
 *
 * Actual REHASH service routine. Called with sig == 0 if it has been called
 * as a result of an operator issuing this command, else assume it has been
 * called as a result of the server receiving a HUP signal.
 */
int	rehash(aClient *cptr,aClient *sptr,int sig)
{
  register aConfItem **tmp = &conf, *tmp2;
  register aClass	*cltmp;
  register aClient	*acptr;
  register	int	i;
  int	ret = 0;
  int   fd;

  if (sig == SIGHUP)
    {
      sendto_ops("Got signal SIGHUP, reloading ircd conf. file");
#ifdef	ULTRIX
      if (fork() > 0)
	exit(0);
      write_pidfile();
#endif
    }

  if ((fd = openconf(configfile)) == -1)
    {
      sendto_ops("Can't open %s file aborting rehash!",configfile);
      return -1;
    }

  /* Shadowfax's LOCKFILE code */
#ifdef LOCKFILE
  do_pending_klines();
#endif

  for (i = 0; i <= highest_fd; i++)
    if ((acptr = local[i]) && !IsMe(acptr))
      {
	/*
	 * Nullify any references from client structures to
	 * this host structure which is about to be freed.
	 * Could always keep reference counts instead of
	 * this....-avalon
	 */
	acptr->hostp = NULL;
#if defined(R_LINES_REHASH) && !defined(R_LINES_OFTEN)
	if (find_restrict(acptr))
	  {
	    sendto_ops("Restricting %s, closing lp",
		       get_client_name(acptr,FALSE));
	    if (exit_client(cptr,acptr,sptr,"R-lined") ==
		FLUSH_BUFFER)
	      ret = FLUSH_BUFFER;
	  }
#endif
     }

  while ((tmp2 = *tmp))
    if (tmp2->clients || tmp2->status & CONF_LISTEN_PORT)
      {
	/*
	** Configuration entry is still in use by some
	** local clients, cannot delete it--mark it so
	** that it will be deleted when the last client
	** exits...
	*/
	if (!(tmp2->status & (CONF_LISTEN_PORT|CONF_CLIENT)))
	  {
	    *tmp = tmp2->next;
	    tmp2->next = NULL;
	  }
	else
	  tmp = &tmp2->next;
	tmp2->status |= CONF_ILLEGAL;
      }
    else
      {
	*tmp = tmp2->next;
	free_conf(tmp2);
      }

  /*
   * We don't delete the class table, rather mark all entries
   * for deletion. The table is cleaned up by check_class. - avalon
   */
  for (cltmp = NextClass(FirstClass()); cltmp; cltmp = NextClass(cltmp))
    MaxLinks(cltmp) = -1;

  /* do we really want to flush the DNS entirely on a SIGHUP?
   * why not let that be controlled by oper /rehash, and use SIGHUP
   * only to change conf file, if one doesn't have a valid O yet? :-)
   * -Dianora
   */

  if (sig != SIGINT)
    flush_cache();		/* Flush DNS cache */

  clear_conf_list(&KList1);
  clear_conf_list(&KList2);
  clear_conf_list(&KList3);

  clear_dlines();

  (void) initconf(0,fd);
#ifdef SEPARATE_QUOTE_KLINES_BY_DATE
  {
    char timebuffer[20];
    char filenamebuf[1024];
    struct tm *tmptr;
    tmptr = localtime(&NOW);
    (void)strftime(timebuffer, 20, "%y%m%d", tmptr);
    ircsprintf(filenamebuf, "%s.%s", klinefile, timebuffer);

    if ((fd = openconf(filenamebuf)) == -1)
      sendto_ops("Can't open %s file klines could be missing!",filenamebuf);
    else
      (void)initconf(0,fd);
  }
#else
#ifdef KLINEFILE
  if ((fd = openconf(klinefile)) == -1)
    sendto_ops("Can't open %s file klines could be missing!",klinefile);
  else
    (void) initconf(0,fd);
#endif
#endif
  close_listeners();

  /*
   * flush out deleted I and P lines although still in use.
   */
  for (tmp = &conf; (tmp2 = *tmp); )
    if (!(tmp2->status & CONF_ILLEGAL))
      tmp = &tmp2->next;
    else
      {
	*tmp = tmp2->next;
	tmp2->next = NULL;
	if (!tmp2->clients)
	  free_conf(tmp2);
      }
  rehashed = 1;
  return ret;
}

/*
 * openconf
 *
 * returns -1 on any error or else the fd opened from which to read the
 * configuration file from.  This may either be the file direct or one end
 * of a pipe from m4.
 */
int	openconf(char *filename)
{
  return open(filename, O_RDONLY);
}
extern char *getfield();

/*
** from comstud
*/

static char *set_conf_flags(aConfItem *aconf,char *tmp)
{
  FOREVER
    {
      switch(*tmp)
	{
	case '!':
	  aconf->flags |= CONF_FLAGS_LIMIT_IP;
	  break;
	case '-':
	  aconf->flags |= CONF_FLAGS_NO_TILDE;
	  break;
	case '+':
	  aconf->flags |= CONF_FLAGS_NEED_IDENTD;
	  break;
	case '$':
	  aconf->flags |= CONF_FLAGS_PASS_IDENTD;
	  break;
	case '%':
	  aconf->flags |= CONF_FLAGS_NOMATCH_IP;
	  break;
	case '^':	/* is exempt from k/g lines */
	  aconf->flags |= CONF_FLAGS_E_LINED;
	  break;
	case '&':	/* can run a bot */
	  aconf->flags |= CONF_FLAGS_B_LINED;
	  break;
	case '>':	/* can exceed max connects */
	  aconf->flags |= CONF_FLAGS_F_LINED;
	  break;
	default:
	  return tmp;
	}
      tmp++;
    }
  /* NOT REACHED */
}

/*
** initconf() 
**    Read configuration file.
**
*
* Inputs 	- opt 
* 		- file descriptor pointing to config file to use
*
**    returns -1, if file cannot be opened
**             0, if file opened
*/

#define MAXCONFLINKS 150

int 	initconf(int opt, int fd)
{
  static	char	quotes[9][2] = {{'b', '\b'}, {'f', '\f'}, {'n', '\n'},
					{'r', '\r'}, {'t', '\t'}, {'v', '\v'},
					{'\\', '\\'}, { 0, 0}};
  Reg	char	*tmp, *s;
  int	i, dontadd;
  char	line[512], c[80];
  int	ccount = 0, ncount = 0;
  u_long vaddr;

  aConfItem *aconf = NULL;

  (void)dgets(-1, NULL, 0); /* make sure buffer is at empty pos */
  while ((i = dgets(fd, line, sizeof(line) - 1)) > 0)
    {
      line[i] = '\0';
      if ((tmp = (char *)index(line, '\n')))
	*tmp = '\0';
      else while(dgets(fd, c, sizeof(c) - 1) > 0)
	if ((tmp = (char *)index(c, '\n')))
	  {
	    *tmp = '\0';
	    break;
	  }
      /*
       * Do quoting of characters and # detection.
       */
      for (tmp = line; *tmp; tmp++)
	{
	  if (*tmp == '\\')
	    {
	      for (i = 0; quotes[i][0]; i++)
		if (quotes[i][0] == *(tmp+1))
		  {
		    *tmp = quotes[i][1];
		    break;
		  }
	      if (!quotes[i][0])
		*tmp = *(tmp+1);
	      if (!*(tmp+1))
		break;
	      else
		for (s = tmp; (*s = *(s+1)); s++)
		  ;
	    }
	  else if (*tmp == '#')
	    *tmp = '\0';
	}
      if (!*line || line[0] == '#' || line[0] == '\n' ||
	  line[0] == ' ' || line[0] == '\t')
	continue;
      /* Could we test if it's conf line at all?	-Vesa */
      if (line[1] != ':')
	{
	  Debug((DEBUG_ERROR, "Bad config line: %s", line));
	  continue;
	}
      if (aconf)
	free_conf(aconf);
      aconf = make_conf();

      tmp = getfield(line);
      if (!tmp)
	continue;
      dontadd = 0;
      switch (*tmp)
	{
	case 'A': /* Name, e-mail address of administrator */
	case 'a': /* of this server. */
	  aconf->status = CONF_ADMIN;
	  break;

	case 'B': /* Bots we know are ok */
	case 'b':
	  aconf->status = CONF_BLINE;
	  break;

	case 'C': /* Server where I should try to connect */
	case 'c': /* in case of lp failures             */
	  ccount++;
	  aconf->status = CONF_CONNECT_SERVER;
	  break;

	case 'D': /* Deny lines (immediate refusal) */
	case 'd':
	  aconf->status = CONF_DLINE;
	  break;

	case 'F': /* Super-Exempt hosts */
	case 'f':
	  aconf->status = CONF_FLINE;
	  break;

	case 'H': /* Hub server line */
	case 'h':
	  aconf->status = CONF_HUB;
	  break;

#ifdef LITTLE_I_LINES
	case 'i': /* to connect me */
	  aconf->status = CONF_CLIENT;
	  aconf->flags |= CONF_FLAGS_LITTLE_I_LINE;
	  break;

	case 'I': /* Just plain normal irc client trying  */
	  aconf->status = CONF_CLIENT;
	  break;
#else
	case 'i': /* to connect me */
	case 'I': /* Just plain normal irc client trying  */
	  aconf->status = CONF_CLIENT;
	  break;
#endif
	case 'K': /* Kill user line on irc.conf           */
	case 'k':
	  aconf->status = CONF_KILL;
	  break;

	case 'L': /* guaranteed leaf server */
	case 'l':
	  aconf->status = CONF_LEAF;
	  break;

	  /* Me. Host field is name used for this host */
	  /* and port number is the number of the port */
	case 'M':
	case 'm':
	  aconf->status = CONF_ME;
	  break;

	case 'N': /* Server where I should NOT try to     */
	case 'n': /* connect in case of lp failures     */
	  /* but which tries to connect ME        */
	  ++ncount;
	  aconf->status = CONF_NOCONNECT_SERVER;
	  break;

          /* Operator. Line should contain at least */
          /* password and host where connection is  */

	case 'O':
	  aconf->status = CONF_OPERATOR;
	  break;
	  /* Local Operator, (limited privs --SRB) */

	case 'o':
	  aconf->status = CONF_LOCOP;
	  break;

	case 'P': /* listen port line */
	case 'p':
	  aconf->status = CONF_LISTEN_PORT;
	  break;

#ifdef R_LINES
	case 'R': /* extended K line */
	case 'r': /* Offers more options of how to restrict */
	  aconf->status = CONF_RESTRICT;
	  break;
#endif
	case 'U': /* Uphost, ie. host where client reading */
	case 'u': /* this should connect.                  */
	  /* This is for client only, I must ignore this */
	  /* ...U-line should be removed... --msa */
	  break;

	case 'Y':
	case 'y':
	  aconf->status = CONF_CLASS;
	  break;

	default:
	  Debug((DEBUG_ERROR, "Error in config file: %s", line));
	  break;
	}
      if (IsIllegal(aconf))
	continue;

      for (;;) /* Fake loop, that I can use break here --msa */
	{
	  if ((tmp = getfield(NULL)) == NULL)
	    break;
	  /*from comstud*/
	  if(aconf->status & CONF_CLIENT)
	    tmp = set_conf_flags(aconf, tmp);
	  DupString(aconf->host, tmp);

	  if ((tmp = getfield(NULL)) == NULL)
	    break;
	  DupString(aconf->passwd, tmp);

	  if ((tmp = getfield(NULL)) == NULL)
	    break;
          /*from comstud */

	  if(aconf->status & CONF_CLIENT)
	    tmp = set_conf_flags(aconf, tmp);
	  DupString(aconf->name, tmp);

	  if(aconf->status & CONF_OPERATOR)
	    {
	      Debug((DEBUG_DEBUG,"Setting defaults for oper"));
	      /* defaults */
	      aconf->port = 
		CONF_OPER_GLOBAL_KILL|CONF_OPER_REMOTE|CONF_OPER_UNKLINE|
		CONF_OPER_K|CONF_OPER_GLINE;
	      if ((tmp = getfield(NULL)) == NULL)
		break;
	      aconf->port = get_oper_privs(aconf->port,tmp);
	    }
	  else if(aconf->status & CONF_LOCOP)
	    {
	      Debug((DEBUG_DEBUG,"Setting defaults for local oper"));
	      aconf->port = CONF_OPER_UNKLINE|CONF_OPER_K;
	      if ((tmp = getfield(NULL)) == NULL)
		break;
	      aconf->port = get_oper_privs(aconf->port,tmp);
	    }
	  else
	    {
	      if ((tmp = getfield(NULL)) == NULL)
		break;
	      aconf->port = atoi(tmp);
	    }

	  Debug((DEBUG_DEBUG,"aconf->port %x",aconf->port));

	  if ((tmp = getfield(NULL)) == NULL)
	    break;
	  Debug((DEBUG_DEBUG,"class tmp = %s",tmp));

	  Class(aconf) = find_class(atoi(tmp));
	  break;
          /* NOTREACHED */
	}
      /*
      ** If conf line is a class definition, create a class entry
      ** for it and make the conf_line illegal and delete it.
      */
      if (aconf->status & CONF_CLASS)
	{
	  add_class(atoi(aconf->host), atoi(aconf->passwd),
		    atoi(aconf->name), aconf->port,
		    tmp ? atoi(tmp) : 0);
	  continue;
	}
      /*
      ** associate each conf line with a class by using a pointer
      ** to the correct class record. -avalon
      */
      if (aconf->status & (CONF_CLIENT_MASK|CONF_LISTEN_PORT))
	{
	  if (Class(aconf) == 0)
	    Class(aconf) = find_class(0);
	  if (MaxLinks(Class(aconf)) < 0)
	    Class(aconf) = find_class(0);
	}
      if (aconf->status & (CONF_LISTEN_PORT|CONF_CLIENT))
	{
	  aConfItem *bconf;
	  
	  if ( (bconf = find_conf_entry(aconf, aconf->status)) )
	    {
	      delist_conf(bconf);
	      bconf->status &= ~CONF_ILLEGAL;
	      if (aconf->status == CONF_CLIENT)
		{
		  bconf->class->links -= bconf->clients;
		  bconf->class = aconf->class;
		  bconf->class->links += bconf->clients;
		  /*
		   * still... I've munged the flags possibly
		   * so update the found aConfItem for now 
		   * -Dianora
		   */
		  bconf->flags = aconf->flags;
		  if(bconf->flags & (CONF_LOCOP|CONF_OPERATOR))
		    bconf->port = aconf->port;
		}
	      free_conf(aconf);
	      aconf = bconf;
	    }
	  else if (aconf->host &&
		   aconf->status == CONF_LISTEN_PORT)
	    (void)add_listener(aconf);
	}
      if (aconf->status & CONF_SERVER_MASK)
	if (ncount > MAXCONFLINKS || ccount > MAXCONFLINKS ||
	    !aconf->host || index(aconf->host, '*') ||
	    index(aconf->host,'?') || !aconf->name)
	  continue;
      
      if (aconf->status &
	  (CONF_SERVER_MASK|CONF_LOCOP|CONF_OPERATOR))
	if (!index(aconf->host, '@') && *aconf->host != '/')
	  {
	    char	*newhost;
	    int	len = 3;	/* *@\0 = 3 */
	    
	    len += strlen(aconf->host);
	    newhost = (char *)MyMalloc(len);
	    (void)ircsprintf(newhost, "*@%s", aconf->host);
	    MyFree(aconf->host);
	    aconf->host = newhost;
	  }
      if (aconf->status & CONF_SERVER_MASK)
	{
	  if (BadPtr(aconf->passwd))
	    continue;
	  else if (!(opt & BOOT_QUICK))
	    (void)lookup_confhost(aconf);
	}
      /*
      ** Own port and name cannot be changed after the startup.
      ** (or could be allowed, but only if all links are closed
      ** first).
      ** Configuration info does not override the name and port
      ** if previously defined. Note, that "info"-field can be
      ** changed by "/rehash".
      ** Can't change vhost mode/address either 
      */
      if (aconf->status == CONF_ME)
	{
	  strncpyzt(me.info, aconf->name, sizeof(me.info));

	  if (me.name[0] == '\0' && aconf->host[0])
          {
	    strncpyzt(me.name, aconf->host,
		      sizeof(me.name));
	    if ((aconf->passwd[0] != '\0') && (aconf->passwd[0] != '*'))
            {
		bzero((char *) &vserv, sizeof(vserv));
		vserv.sin_family = AF_INET;
		vaddr = inet_addr(aconf->passwd);
		bcopy((char *) &vaddr, (char *)&vserv.sin_addr, sizeof(struct in_addr));
		specific_virtual_host = 1;
	    }
	  }

	  if (portnum < 0 && aconf->port >= 0)
	    portnum = aconf->port;
	}
      
      if ((aconf->status & CONF_KILL) && aconf->host)
	{
	  char	*host = host_field(aconf);
	  
	  dontadd = 1;
	  switch (sortable(host))
	    {
	    case 0 :
	      l_addto_conf_list(&KList3, aconf, host_field);
	      break;
	    case 1 :
	      addto_conf_list(&KList1, aconf, host_field);
	      break;
	    case -1 :
	      addto_conf_list(&KList2, aconf, rev_host_field);
	      break;
	    }

	  MyFree(host);
	}

      if (aconf->host && (aconf->status & CONF_DLINE))
	{
	  unsigned long mask;
	  unsigned host_ip;

	  dontadd = 1;
	  host_ip = host_name_to_ip(aconf->host,&mask),
	  add_to_dline_hash(host_ip,
			    mask,
			    aconf->host,
			    aconf->passwd);
	}

      (void)collapse(aconf->host);
      (void)collapse(aconf->name);
      Debug((DEBUG_NOTICE,
	     "Read Init: (%d) (%s) (%s) (%s) (%d) (%d)",
	     aconf->status, aconf->host, aconf->passwd,
	     aconf->name, aconf->port, Class(aconf)));
      if (!dontadd)
	{
	  aconf->next = conf;
	  conf = aconf;
	}
      aconf = NULL;
    }
  if (aconf)
    free_conf(aconf);
  (void)dgets(-1, NULL, 0); /* make sure buffer is at empty pos */
  (void)close(fd);
  check_class();
  nextping = nextconnect = time(NULL);


  if(me.name[0] == '\0')
    {
      report_error_on_tty("Server has no M: line\n");
#ifdef USE_SYSLOG
      syslog(LOG_CRIT,"Server has no M: line");
#endif
      exit(-1);
    }
  return 0;
}

/*
 * lookup_confhost
 *   Do (start) DNS lookups of all hostnames in the conf line and convert
 * an IP addresses in a.b.c.d number for to IP#s.
 *
 * cleaned up Aug 3'97 - Dianora
 */
static	int	lookup_confhost(aConfItem *aconf)
{
  Reg	char	*s;
  Reg	struct	hostent *hp;
  Link	ln;

  if (BadPtr(aconf->host) || BadPtr(aconf->name))
    {
      if (aconf->ipnum.s_addr == -1)
	bzero((char *)&aconf->ipnum, sizeof(struct in_addr));
      Debug((DEBUG_ERROR,"Host/server name error: (%s) (%s)",
	     aconf->host, aconf->name));
      return -1;
    }
  if ((s = index(aconf->host, '@')))
    s++;
  else
    s = aconf->host;
  /*
  ** Do name lookup now on hostnames given and store the
  ** ip numbers in conf structure.
  */
  if (!isalpha(*s) && !isdigit(*s))
    {
      if (aconf->ipnum.s_addr == -1)
	bzero((char *)&aconf->ipnum, sizeof(struct in_addr));
      Debug((DEBUG_ERROR,"Host/server name error: (%s) (%s)",
	     aconf->host, aconf->name));
      return -1;
    }

  /*
  ** Prepare structure in case we have to wait for a
  ** reply which we get later and store away.
  */
  ln.value.aconf = aconf;
  ln.flags = ASYNC_CONF;
  
  if (isdigit(*s))
    aconf->ipnum.s_addr = inet_addr(s);
  else if ((hp = gethost_byname(s, &ln)))
    bcopy(hp->h_addr, (char *)&(aconf->ipnum),
	  sizeof(struct in_addr));
  
  if (aconf->ipnum.s_addr == -1)
    bzero((char *)&aconf->ipnum, sizeof(struct in_addr));
  {
    Debug((DEBUG_ERROR,"Host/server name error: (%s) (%s)",
	   aconf->host, aconf->name));
    return -1;
  }
  /* NOTREACHED */
  return 0;
}

/* Why not just a simple macro? maybe next version... -Dianora */
/* This is also used in ircd.c now */

#ifndef K_COMMENT_ONLY
int is_comment(char *comment)
{
  return (*comment == '');
}
#endif

/*
find_dline

input		- host_ip as unsigned long in =network= order
output		- 1 if D line was found
side effects	-
*/

int find_dline(unsigned long host_ip)
{
  return((int)find_host_in_dline_hash(host_ip));
}

/*
  find_kill

  See if this user is klined already, and if so, return aConfItem pointer
  to the entry for this kline. This wildly changes the way find_kill works
  -Dianora

*/

aConfItem *find_kill(aClient *cptr)
{
  char *host, *name;
  
  if (!cptr->user)
    return 0;

  host = cptr->sockhost;
  name = cptr->user->username;

  if (strlen(host)  > (size_t) HOSTLEN ||
      (name ? strlen(name) : 0) > (size_t) HOSTLEN)
    return (0);

  /* If client is e-lined, then its not k-linable */
  /* opers get that flag automatically, normal users do not */

  if (IsElined(cptr))
    return(0);
  
  return(find_is_klined(host,name));
}

/*
  WARNING, no sanity checking on length of name,host etc.
  thats expected to be done by caller.... *sigh* -Dianora
 */

aConfItem *find_is_klined(char *host,char *name)
{
  aConfList *list;
  char rev[HOSTLEN+1];	/* why waste 2 function calls for this ? - Dianora */

  aConfItem *kill_list_ptr;	/* used for the link list only */
  aConfItem *last_list_ptr;
  aConfItem *tmp_list_ptr;
  aConfItem *tmp;

  /* Temporary kline handling...
     I expect this list to be very tiny. (crosses fingers) so CPU
     time in this, should be minimum.
     -Dianora
  */

  if(temporary_klines)
    {
      kill_list_ptr = last_list_ptr = temporary_klines;

      while(kill_list_ptr)
	{
	  if(kill_list_ptr->hold <= NOW)	/* a kline has expired */
	    {
	      if(temporary_klines == kill_list_ptr)
		{
		  /* Its pointing to first one in link list*/
		  /* so, bypass this one, remember bad things can happen
		     if you try to use an already freed pointer.. */

		  temporary_klines = last_list_ptr = tmp_list_ptr =
		    kill_list_ptr->next;
		}
	      else
		{
		  /* its in the middle of the list, so link around it */
		  tmp_list_ptr = last_list_ptr->next = kill_list_ptr->next;
		}

	      MyFree(kill_list_ptr->host);
	      MyFree(kill_list_ptr->name);
	      MyFree(kill_list_ptr->passwd);
	      MyFree(kill_list_ptr);
	      kill_list_ptr = tmp_list_ptr;
	    }
	  else
	    {
	      if( (kill_list_ptr->name && (!name || !match(kill_list_ptr->name,
                 name))) && (kill_list_ptr->host &&
                   (!host || !match(kill_list_ptr->host,host))))
		return(kill_list_ptr);
              last_list_ptr = kill_list_ptr;
              kill_list_ptr = kill_list_ptr->next;
	    }
	}
    }


  reverse(rev, host);

/* I have NEVER seen a kline with a port field, have you?
   I have removed the testing of the port number from here
   -Dianora
*/

  /* Start with hostnames of the form "*word" (most frequent) -Sol */
  list = &KList2;
  while ((tmp = find_matching_conf(list, rev)) != NULL)
    {
#ifndef TIMED_KLINES
      if (tmp->name && (!name || !match(tmp->name, name)))
	  return(tmp);
#else
#ifdef K_COMMENT_ONLY
      if (tmp->name && (!name || !match(tmp->name, name)))
	if(tmp->passwd)
	  {
	    if(check_time_interval(tmp->passwd))
	      return(tmp);
	    else
	      return((aConfItem *)NULL);
	  }
#else
      if (tmp->name && (!name || !match(tmp->name, name)))
	if(!BadPtr(tmp->passwd) && is_comment(tmp->passwd))
	  {
	    if(check_time_interval(tmp->passwd))
	      return(tmp);
	    else
	      return((aConfItem *)NULL);
	  }
#endif
#endif
      list = NULL;
    }

  /* Try hostnames of the form "word*" -Sol */
  list = &KList1;
  while ((tmp = find_matching_conf(list, host)) != NULL)
    {
#ifndef TIMED_KLINES
      if (tmp->name && (!name || !match(tmp->name, name)))
	  return(tmp);
#else
#ifdef K_COMMENT_ONLY
      if (tmp->name && (!name || !match(tmp->name, name)))
	if(tmp->passwd)
	  {
	    if(check_time_interval(tmp->passwd))
	      return(tmp);
	    else
	      return((aConfItem *)NULL);
	  }
#else
      if (tmp->name && (!name || !match(tmp->name, name)))
	if(!BadPtr(tmp->passwd) && is_comment(tmp->passwd))
	  {
	    if(check_time_interval(tmp->passwd))
	      return(tmp);
	    else
	      return((aConfItem *)NULL);
	  }
#endif
#endif
      list = NULL;
    }

  /* If none of the above worked, try non-sorted entries -Sol */
  list = &KList3;
  while ((tmp = l_find_matching_conf(list, host)) != NULL)
    {
#ifndef TIMED_KLINES
      if (tmp->host && tmp->name && (!name || !match(tmp->name, name)))
	  return(tmp);
#else
#ifdef K_COMMENT_ONLY
      if (tmp->host && tmp->name && (!name || !match(tmp->name, name)))
	if(tmp->passwd)
	  {
	    if(check_time_interval(tmp->passwd))
	      return(tmp);
	    else
	      return((aConfItem *)NULL);
	  }
#else
      if (tmp->host && tmp->name && (!name || !match(tmp->name, name)))
	if(!BadPtr(tmp->passwd) && is_comment(tmp->passwd))
	  {
	    if(check_time_interval(tmp->passwd))
	       return(tmp);
	    else
	      return((aConfItem *)NULL);
	  }
#endif
#endif
      list = NULL;
    }
  return((aConfItem *)NULL);
}

/*
report_matching_host_klines

inputs		- aClient pointer pointing to user who requested stats k
output		- NONE
side effects	-

report klines that are in the same "close" domain as user

-Dianora
*/

void report_matching_host_klines(aClient *cptr,char *host)
{
  char *pass;
  char *name = (char *)NULL;
  char *found_host = (char *)NULL;
  aConfItem *tmp;
  aConfList *list;
  static char null[] = "<NULL>";
  char rev[HOSTLEN+1];	/* why waste 2 function calls for this ? - Dianora */

#ifndef K_COMMENT_ONLY
  int  port;
#endif

  if (strlen(host) > (size_t) HOSTLEN ||
      (name ? strlen(name) : 0) > (size_t) HOSTLEN)
    return;

  reverse(rev, host);

  /* Start with hostnames of the form "*word" (most frequent) -Sol */
  list = &KList2;
  while ((tmp = find_matching_conf(list, rev)) != NULL)
    {
      pass = BadPtr(tmp->passwd) ? null : tmp->passwd;
      name = BadPtr(tmp->name) ? null : tmp->name;
      found_host = BadPtr(tmp->host) ? null : tmp->host;
#ifdef K_COMMENT_ONLY
      if (tmp->status == CONF_KILL)
	sendto_one(cptr, rpl_str(RPL_STATSKLINE), me.name,
		   cptr->name, 'K', found_host,
		   name, pass);
#else
      port = BadPtr(tmp->port) ? 0 : tmp->port;
      if (tmp->status == CONF_KILL)
	sendto_one(cptr, rpl_str(RPL_STATSKLINE), me.name,
		   cptr->name, 'K', found_host,
		   pass, name, port, get_conf_class(tmp));
#endif
      list = NULL;
    }

  /* Try hostnames of the form "word*" -Sol */
  list = &KList1;
  while ((tmp = find_matching_conf(list, host)) != NULL)
    {
      pass = BadPtr(tmp->passwd) ? null : tmp->passwd;
      name = BadPtr(tmp->name) ? null : tmp->name;
      found_host = BadPtr(tmp->host) ? null : tmp->host;
#ifdef K_COMMENT_ONLY
      if (tmp->status == CONF_KILL)
	sendto_one(cptr, rpl_str(RPL_STATSKLINE), me.name,
		   cptr->name, 'K', found_host,
		   name, pass);
#else
      port = BadPtr(tmp->port) ? 0 : tmp->port;
      if (tmp->status == CONF_KILL)
	sendto_one(cptr, rpl_str(RPL_STATSKLINE), me.name,
		   cptr->name, 'K', found_host,
		   pass, name, port, get_conf_class(tmp));
#endif
      list = NULL;
    }

  /* If none of the above worked, try non-sorted entries -Sol */
  list = &KList3;
  while ((tmp = l_find_matching_conf(list, host)) != NULL)
    {
      pass = BadPtr(tmp->passwd) ? null : tmp->passwd;
      name = BadPtr(tmp->name) ? null : tmp->name;
      found_host = BadPtr(tmp->host) ? null : tmp->host;
#ifdef K_COMMENT_ONLY
      if (tmp->status == CONF_KILL)
	sendto_one(cptr, rpl_str(RPL_STATSKLINE), me.name,
		   cptr->name, 'K', found_host,
		   name, pass);
#else
      port = BadPtr(tmp->port) ? 0 : tmp->port;
      if (tmp->status == CONF_KILL)
	sendto_one(cptr, rpl_str(RPL_STATSKLINE), me.name,
		   cptr->name, 'K', found_host,
		   pass, name, port, get_conf_class(tmp));
#endif
      list = NULL;
    }
}


/*
find_dkill is only called when a /quote dline is applied
all quote dlines are sortable, of the form nnn.nnn.nnn.* or nnn.nnn.nnn.nnn
There is no reason to check for all three possible formats as in klines

- Dianora

inputs		- client pointer
output		- return 1 if match is found, and loser should die
		  return 0 if no match is found and loser shouldn't die
side effects	- only sortable dline tree is searched
*/

aConfItem *find_dkill(aClient *cptr)
{
  if (!cptr->user)
    return 0;

  if(IsElined(cptr))
     return((aConfItem *)NULL);

  return(find_host_in_dline_hash(cptr->ip.s_addr));
}

int	find_conf_match(aClient *cptr,
			aConfList *List1,
			aConfList *List2,
			aConfList *List3)
{
  char	*host, *name;
  aConfItem *tmp;
  char rev[HOSTLEN+1];	/* why waste 2 function calls for this ? - Dianora */
  aConfList *list;

  if (!cptr->user)
    return 0;

  host = cptr->sockhost;
  name = cptr->user->username;
  
  if (strlen(host)  > (size_t) HOSTLEN ||
      (name ? strlen(name) : 0) > (size_t) HOSTLEN)
    return (0);

  reverse(rev, host);

  /* Start with hostnames of the form "*word" (most frequent) -Sol */
  list = List2;
  while ((tmp = find_matching_conf(list, rev)) != NULL)
    {
      if (tmp->name && (!name || !match(tmp->name, name)))
	{
	  return (tmp ? -1 : 0);
	}
      list = NULL;
    }
  
  /* Try hostnames of the form "word*" -Sol */
  list = List1;
  while ((tmp = find_matching_conf(list, host)) != NULL)
    {
      if (tmp->name && (!name || !match(tmp->name, name)))
	{
	  return (tmp ? -1 : 0);
	}
      list = NULL;
    }

  /* If none of the above worked, try non-sorted entries -Sol */
  list = List3;
  while ((tmp = l_find_matching_conf(list, host)) != NULL)
    {
      if (tmp->host && tmp->name && (!name || !match(tmp->name, name)))
	{
	  return (tmp ? -1 : 0);
	}
      list = NULL;
    }

  return (tmp ? -1 : 0);
}

/* add_temp_kline

inputs		- pointer to aConfItem
output		- none
Side effects	- links in given aConfItem into temporary kline link list

-Dianora
*/

void add_temp_kline(aConfItem *aconf)
{
  aconf->next = temporary_klines;
  temporary_klines = aconf;
}

/* flush_temp_klines

inputs		- NONE
output		- NONE
side effects	- All temporary klines are flushed out. 
		  really should be used only for cases of extreme
		  goof up for now.

		  Note, you can't free an object, and expect to
		  be able to dereference it under some malloc systems.
*/
void flush_temp_klines()
{
  aConfItem *kill_list_ptr;

  if( (kill_list_ptr = temporary_klines) )
    {
      while(kill_list_ptr)
        {
          temporary_klines = kill_list_ptr->next;
          MyFree(kill_list_ptr->host);
          MyFree(kill_list_ptr->name);
	  MyFree(kill_list_ptr->passwd);
          MyFree(kill_list_ptr);
	  kill_list_ptr = temporary_klines;
        }
    }
}


#ifdef GLINES
/*
 * flush_glines
 * 
 * inputs	- NONE
 * output	- NONE
 * side effects	-
 *
 * Get rid of all placed G lines, hopefully to be replaced by gline.log
 * placed k-lines
 */
void flush_glines()
{
  aConfItem *kill_list_ptr;

  if(kill_list_ptr = glines)
    {
      while(kill_list_ptr)
        {
          glines = kill_list_ptr->next;
          MyFree(kill_list_ptr->host);
          MyFree(kill_list_ptr->name);
	  MyFree(kill_list_ptr->passwd);
          MyFree(kill_list_ptr);
	  kill_list_ptr = glines;
        }
    }
}

/* find_gkill
 *
 * inputs	- aClient pointer to a Client struct
 * output	- aConfItem pointer if a gline was found for this client
 * side effects	- none
 */

aConfItem *find_gkill(aClient *cptr)
{
  char *host, *name;
  
  if (!cptr->user)
    return 0;

  host = cptr->sockhost;
  name = cptr->user->username;

  if (strlen(host)  > (size_t) HOSTLEN ||
      (name ? strlen(name) : 0) > (size_t) HOSTLEN)
    return (0);
  
  if(IsElined(cptr))
    return 0;

  return(find_is_glined(host,name));
}

/*
 * find_is_glined
 * inputs	- hostname
 *		- username
 * output	- pointer to aConfItem if user@host glined
 * side effects	-
 *  WARNING, no sanity checking on length of name,host etc.
 * thats expected to be done by caller.... *sigh* -Dianora
 */

aConfItem *find_is_glined(char *host,char *name)
{
  aConfItem *kill_list_ptr;	/* used for the link list only */
  aConfItem *last_list_ptr;
  aConfItem *tmp_list_ptr;

  /* gline handling... exactly like temporary klines 
   * I expect this list to be very tiny. (crosses fingers) so CPU
   * time in this, should be minimum.
   * -Dianora
  */

  if(glines)
    {
      kill_list_ptr = last_list_ptr = glines;

      while(kill_list_ptr)
	{
	  if(kill_list_ptr->hold <= NOW)	/* a gline has expired */
	    {
	      if(glines == kill_list_ptr)
		{
		  /* Its pointing to first one in link list*/
		  /* so, bypass this one, remember bad things can happen
		     if you try to use an already freed pointer.. */

		  glines = last_list_ptr = tmp_list_ptr =
		    kill_list_ptr->next;
		}
	      else
		{
		  /* its in the middle of the list, so link around it */
		  tmp_list_ptr = last_list_ptr->next = kill_list_ptr->next;
		}

	      MyFree(kill_list_ptr->host);
	      MyFree(kill_list_ptr->name);
	      MyFree(kill_list_ptr->passwd);
	      MyFree(kill_list_ptr);
	      kill_list_ptr = tmp_list_ptr;
	    }
	  else
	    {
	      if( (kill_list_ptr->name && (!name || !match(kill_list_ptr->name,
                 name))) && (kill_list_ptr->host &&
                   (!host || !match(kill_list_ptr->host,host))))
		return(kill_list_ptr);
              last_list_ptr = kill_list_ptr;
              kill_list_ptr = kill_list_ptr->next;
	    }
	}
    }

  return((aConfItem *)NULL);
}

/* report_glines
 *
 * inputs	- aClient pointer
 * output	- NONE
 * side effects	- 
 *
 * report pending glines, and placed glines.
 * 
 * - Dianora		  
 */
void report_glines(aClient *sptr)
{
  GLINE_PENDING *gline_pending_ptr;
  aConfItem *kill_list_ptr;
  aConfItem *last_list_ptr;
  aConfItem *tmp_list_ptr;
  char timebuffer[MAX_DATE_STRING];
  struct tm *tmptr;
  char *host;
  char *name;
  char *reason;

  expire_pending_glines();	/* This is not the g line list, but
				   the pending possible g line list */

  if(gline_pending_ptr = pending_glines)
    {
      sendto_one(sptr,":%s NOTICE %s :Pending G-lines", me.name,
			 sptr->name);
      while(gline_pending_ptr)
	{
	  tmptr = localtime(&gline_pending_ptr->time_request1);
	  strftime(timebuffer, MAX_DATE_STRING, "%y/%m/%d %H:%M:%S", tmptr);

	  sendto_one(sptr,":%s NOTICE %s :1) %s!%s@%s on %s requested gline at %s for %s@%s [%s]",
		     me.name,sptr->name,
		     gline_pending_ptr->oper_nick1,
		     gline_pending_ptr->oper_user1,
		     gline_pending_ptr->oper_host1,
		     gline_pending_ptr->oper_server1,
		     timebuffer,
		     gline_pending_ptr->user,
		     gline_pending_ptr->host,
		     gline_pending_ptr->reason1);

	  if(gline_pending_ptr->oper_nick2[0])
	    {
	      tmptr = localtime(&gline_pending_ptr->time_request2);
	      strftime(timebuffer, MAX_DATE_STRING, "%y/%m/%d %H:%M:%S", tmptr);
	      sendto_one(sptr,
	      ":%s NOTICE %s :2) %s!%s@%s on %s requested gline at %s for %s@%s [%s]",
			 me.name,sptr->name,
			 gline_pending_ptr->oper_nick2,
			 gline_pending_ptr->oper_user2,
			 gline_pending_ptr->oper_host2,
			 gline_pending_ptr->oper_server2,
			 timebuffer,
			 gline_pending_ptr->user,
			 gline_pending_ptr->host,
			 gline_pending_ptr->reason2);
	    }
	  gline_pending_ptr = gline_pending_ptr->next;
	}
      sendto_one(sptr,":%s NOTICE %s :End of Pending G-lines", me.name,
		 sptr->name);
    }

  if(glines)
    {
      kill_list_ptr = last_list_ptr = glines;

      while(kill_list_ptr)
        {
	  if(kill_list_ptr->hold <= NOW)	/* gline has expired */
	    {
	      if(temporary_klines == kill_list_ptr)
		{
		  /* Its pointing to first one in link list*/
		  /* so, bypass this one, remember bad things can happen
		     if you try to use an already freed pointer.. */

		  glines = last_list_ptr = tmp_list_ptr =
		    kill_list_ptr->next;
		}
	      else
		{
		  /* its in the middle of the list, so link around it */
		  tmp_list_ptr = last_list_ptr->next = kill_list_ptr->next;
		}

	      MyFree(kill_list_ptr->host);
	      MyFree(kill_list_ptr->name);
	      MyFree(kill_list_ptr->passwd);
	      MyFree(kill_list_ptr);
	      kill_list_ptr = tmp_list_ptr;
	    }
	  else
	    {
	      if(kill_list_ptr->host)
		host = kill_list_ptr->host;
	      else
		host = "*";

	      if(kill_list_ptr->name)
		name = kill_list_ptr->name;
	      else
		name = "*";

	      if(kill_list_ptr->passwd)
		reason = kill_list_ptr->passwd;
	      else
		reason = "No Reason";

	      sendto_one(sptr,rpl_str(RPL_STATSKLINE), me.name,
			 sptr->name, 'G' , host, name, reason);

	      last_list_ptr = kill_list_ptr;
	      kill_list_ptr = kill_list_ptr->next;
	    }
        }
    }
}

/*
 * expire_pending_glines
 * 
 * inputs	- NONE
 * output	- NONE
 * side effects	-
 *
 * Go through the pending gline list, expire any that haven't had
 * enough "votes" in the time period allowed
 */

void expire_pending_glines()
{
  GLINE_PENDING *gline_pending_ptr;
  GLINE_PENDING *last_gline_pending_ptr;
  GLINE_PENDING *tmp_pending_ptr;

  if(pending_glines == (GLINE_PENDING *)NULL)
    return;

  last_gline_pending_ptr = (GLINE_PENDING *)NULL;
  gline_pending_ptr = pending_glines;

  while(gline_pending_ptr)
    {
      if( (gline_pending_ptr->last_gline_time + GLINE_PENDING_EXPIRE) <= NOW )
	{
	  if(last_gline_pending_ptr)
	    last_gline_pending_ptr->next = gline_pending_ptr->next;
	  else
	    pending_glines = gline_pending_ptr->next;

	  /*Some malloc packages get terribly upset if you 
	    try to dereference a pointer you have freed,
	    hence I need to dereference the pointer then free it,
	    so I need to keep a pointer to the item I am about to free.
	    -Dianora */

	  tmp_pending_ptr = gline_pending_ptr;
	  gline_pending_ptr = gline_pending_ptr->next;
	  MyFree(tmp_pending_ptr->reason1);
	  MyFree(tmp_pending_ptr->reason2);
	  MyFree(tmp_pending_ptr);
	}
      else
	{
	  last_gline_pending_ptr = gline_pending_ptr;
	  gline_pending_ptr = gline_pending_ptr->next;
	}
    }
}

/*
 * majority_gline()
 *
 * inputs	- oper_nick, oper_user, oper_host, oper_server
 *		  user,host reason
 *
 * output	- YES if there are 3 different opers on 3 different servers
 *		  agreeing to this gline, NO if there are not.
 * Side effects	-
 *	See if there is a majority agreement on a GLINE on the given user
 *	There must be at least 3 different opers agreeing on this GLINE
 *
 *	Expire old entries.
 */

int majority_gline(aClient *sptr,
			  char *oper_nick,
			  char *oper_user,
			  char *oper_host,
			  char *oper_server,
			  char *user,
			  char *host,
			  char *reason)
{
  GLINE_PENDING *new_pending_gline;
  GLINE_PENDING *gline_pending_ptr;

  /* special case condition where there are no pending glines */

  if(pending_glines == (GLINE_PENDING *)NULL) /* first gline request placed */
    {
      new_pending_gline = (GLINE_PENDING *)malloc(sizeof(GLINE_PENDING));
      if(new_pending_gline == (GLINE_PENDING *)NULL)
	{
	  sendto_realops("No memory for GLINE, GLINE dropped");
	  return NO;
	}

      strncpyzt(new_pending_gline->oper_nick1,oper_nick,NICKLEN+1);
      new_pending_gline->oper_nick2[0] = '\0';

      strncpyzt(new_pending_gline->oper_user1,oper_user,USERLEN);
      new_pending_gline->oper_user2[0] = '\0';

      strncpyzt(new_pending_gline->oper_host1,oper_host,HOSTLEN);
      new_pending_gline->oper_host2[0] = '\0';

      new_pending_gline->oper_server1 = find_or_add(oper_server);


      strncpyzt(new_pending_gline->user,user,USERLEN);
      strncpyzt(new_pending_gline->host,host,HOSTLEN);
      new_pending_gline->reason1 = strdup(reason);
      new_pending_gline->reason2 = (char *)NULL;

      new_pending_gline->next = (GLINE_PENDING *)NULL;
      new_pending_gline->last_gline_time = NOW;
      new_pending_gline->time_request1 = NOW;
      pending_glines = new_pending_gline;
      return NO;
    }

  expire_pending_glines();

  for(gline_pending_ptr = pending_glines;
      gline_pending_ptr; gline_pending_ptr = gline_pending_ptr->next)
    {
      if( (strcasecmp(gline_pending_ptr->user,user) == 0) &&
	  (strcasecmp(gline_pending_ptr->host,host) ==0 ) )
	{
	  if(((strcasecmp(gline_pending_ptr->oper_user1,oper_user) == 0) &&
	      (strcasecmp(gline_pending_ptr->oper_host1,oper_host) == 0)) ||
	      (strcasecmp(gline_pending_ptr->oper_server1,oper_server) == 0) )
	    {
	      /* This oper or server has already "voted" */
	      sendto_realops("oper or server has already voted");
	      return NO;
	    }

	  if(gline_pending_ptr->oper_user2[0] != '\0')
	    {
	      /* if two other opers on two different servers have voted yes */

	      if(((strcasecmp(gline_pending_ptr->oper_user2,oper_user)==0) &&
		  (strcasecmp(gline_pending_ptr->oper_host2,oper_host)==0)) ||
		  (strcasecmp(gline_pending_ptr->oper_server2,oper_server)==0))
		{
		  /* This oper or server has already "voted" */
		  sendto_ops("oper or server has already voted");
		  return NO;
		}

	      if(find_is_glined(host,user))
		return NO;

	      if(find_is_klined(host,user))
		return NO;

	      log_gline(sptr,sptr->name,gline_pending_ptr,
			oper_nick,oper_user,oper_host,oper_server,
			user,host,reason);
	      return YES;
	    }
	  else
	    {
	      strncpyzt(gline_pending_ptr->oper_nick2,oper_nick,NICKLEN+1);
	      strncpyzt(gline_pending_ptr->oper_user2,oper_user,USERLEN);
	      strncpyzt(gline_pending_ptr->oper_host2,oper_host,HOSTLEN);
	      gline_pending_ptr->reason2 = strdup(reason);
	      gline_pending_ptr->oper_server2 = find_or_add(oper_server);
	      gline_pending_ptr->last_gline_time = NOW;
	      gline_pending_ptr->time_request1 = NOW;
	      return NO;
	    }
	}
    }
  /* Didn't find this user@host gline in pending gline list
   * so add it.
   */

  new_pending_gline = (GLINE_PENDING *)malloc(sizeof(GLINE_PENDING));
  if(new_pending_gline == (GLINE_PENDING *)NULL)
    {
      sendto_realops("No memory for GLINE, GLINE dropped");
      return NO;
    }

  strncpyzt(new_pending_gline->oper_nick1,oper_nick,NICKLEN+1);
  new_pending_gline->oper_nick2[0] = '\0';

  strncpyzt(new_pending_gline->oper_user1,oper_user,USERLEN);
  new_pending_gline->oper_user2[0] = '\0';

  strncpyzt(new_pending_gline->oper_host1,oper_host,HOSTLEN);
  new_pending_gline->oper_host2[0] = '\0';

  new_pending_gline->oper_server1 = find_or_add(oper_server);

  strncpyzt(new_pending_gline->user,user,USERLEN);
  strncpyzt(new_pending_gline->host,host,HOSTLEN);
  new_pending_gline->reason1 = strdup(reason);
  new_pending_gline->reason2 = (char *)NULL;

  new_pending_gline->last_gline_time = NOW;
  new_pending_gline->time_request1 = NOW;
  new_pending_gline->next = pending_glines;
  pending_glines = new_pending_gline;
  
  return NO;
}

/* add_gline

inputs		- pointer to aConfItem
output		- none
Side effects	- links in given aConfItem into gline link list

Identical to add_temp_kline code really.

-Dianora
*/

void add_gline(aConfItem *aconf)
{
  aconf->next = glines;
  glines = aconf;
}
#endif

/* report_temp_klines

inputs		- aClient pointer
output		- NONE
side effects	- 
		  
*/
void report_temp_klines(aClient *sptr)
{
  aConfItem *kill_list_ptr;
  aConfItem *last_list_ptr;
  aConfItem *tmp_list_ptr;
  char *host;
  char *name;
  char *reason;

  if(temporary_klines)
    {
      kill_list_ptr = last_list_ptr = temporary_klines;

      while(kill_list_ptr)
        {
	  if(kill_list_ptr->hold <= NOW)	/* kline has expired */
	    {
	      if(temporary_klines == kill_list_ptr)
		{
		  /* Its pointing to first one in link list*/
		  /* so, bypass this one, remember bad things can happen
		     if you try to use an already freed pointer.. */

		  temporary_klines = last_list_ptr = tmp_list_ptr =
		    kill_list_ptr->next;
		}
	      else
		{
		  /* its in the middle of the list, so link around it */
		  tmp_list_ptr = last_list_ptr->next = kill_list_ptr->next;
		}

	      MyFree(kill_list_ptr->host);
	      MyFree(kill_list_ptr->name);
	      MyFree(kill_list_ptr->passwd);
	      MyFree(kill_list_ptr);
	      kill_list_ptr = tmp_list_ptr;
	    }
	  else
	    {
	      if(kill_list_ptr->host)
		host = kill_list_ptr->host;
	      else
		host = "*";

	      if(kill_list_ptr->name)
		name = kill_list_ptr->name;
	      else
		name = "*";

	      if(kill_list_ptr->passwd)
		reason = kill_list_ptr->passwd;
	      else
		reason = "No Reason";

	      sendto_one(sptr,rpl_str(RPL_STATSKLINE), me.name,
			 sptr->name, 'k' , host, name, reason);

	      last_list_ptr = kill_list_ptr;
	      kill_list_ptr = kill_list_ptr->next;
	    }
        }
    }
}

/* urgh. consider this a place holder for a new version of R-lining
   to be done in next version of hybrid. don't use this. it will
   likely break... :-) -Dianora
*/ 

#ifdef R_LINES
/* find_restrict works against host/name and calls an outside program 
 * to determine whether a client is allowed to connect.  This allows 
 * more freedom to determine who is legal and who isn't, for example
 * machine load considerations.  The outside program is expected to 
 * return a reply line where the first word is either 'Y' or 'N' meaning 
 * "Yes Let them in" or "No don't let them in."  If the first word 
 * begins with neither 'Y' or 'N' the default is to let the person on.
 * It returns a value of 0 if the user is to be let through -Hoppie
 */
int	find_restrict(aClient *cptr)
{
  aConfItem *tmp;
  char	reply[80], temprpl[80];
  char	*rplhold = reply, *host, *name, *s;
  char	rplchar = 'Y';
  int	pi[2], rc = 0, n;
  
  if (!cptr->user)
    return 0;
  name = cptr->user->username;
  host = cptr->sockhost;
  Debug((DEBUG_INFO, "R-line check for %s[%s]", name, host));

  for (tmp = conf; tmp; tmp = tmp->next)
    {
      if (tmp->status != CONF_RESTRICT ||
	  (tmp->host && host && match(tmp->host, host)) ||
	  (tmp->name && name && match(tmp->name, name)))
	continue;

      if (BadPtr(tmp->passwd))
	{
	  sendto_ops("Program missing on R-line %s/%s, ignoring",
		     name, host);
	  continue;
	}

      if (pipe(pi) == -1)
	{
	  report_error("Error creating pipe for R-line %s:%s",
		       &me);
	  return 0;
	}
      switch (rc = vfork())
	{
	case -1 :
	  report_error("Error forking for R-line %s:%s", &me);
	  return 0;
	case 0 :
	  {
	    Reg	int	i;

	    (void)close(pi[0]);
	    for (i = 2; i < MAXCONNECTIONS; i++)
	      if (i != pi[1])
		(void)close(i);
	    if (pi[1] != 2)
	      (void)dup2(pi[1], 2);
	    (void)dup2(2, 1);
	    if (pi[1] != 2 && pi[1] != 1)
	      (void)close(pi[1]);
	    (void)execlp(tmp->passwd, tmp->passwd, name, host, 0);
	    exit(-1);
	  }
	default :
	  (void)close(pi[1]);
	  break;
	}
      *reply = '\0';
      (void)dgets(-1, NULL, 0); /* make sure buffer marked empty */
      while ((n = dgets(pi[0], temprpl, sizeof(temprpl)-1)) > 0)
	{
	  temprpl[n] = '\0';
	  if ((s = (char *)index(temprpl, '\n')))
	    *s = '\0';
	  if (strlen(temprpl) + strlen(reply) < sizeof(reply)-2)
	    (void)ircsprintf(rplhold, "%s %s", rplhold,
			     temprpl);
	  else
	    {
	      sendto_ops("R-line %s/%s: reply too long!",
			 name, host);
	      break;
	    }
	}
      (void)dgets(-1, NULL, 0); /* make sure buffer marked empty */
      (void)close(pi[0]);
      (void)kill(rc, SIGKILL); /* cleanup time */
      (void)wait(0);

      rc = 0;
      while (*rplhold == ' ')
	rplhold++;
      rplchar = *rplhold; /* Pull out the yes or no */
      while (*rplhold != ' ')
	rplhold++;
      while (*rplhold == ' ')
	rplhold++;
      (void)strcpy(reply,rplhold);
      rplhold = reply;
      
      if ((rc = (rplchar == 'n' || rplchar == 'N')))
	break;
    }
  if (rc)
    {
      sendto_one(cptr, ":%s %d %s :Restriction: %s",
		 me.name, ERR_YOUREBANNEDCREEP, cptr->name,
		 reply);
      return -1;
    }
  return 0;
}
#endif


/* check_time_interval
inputs		- comment field of k-line
output		- 1 if its NOT a time_interval or it is :-)
		  0 if its a time interval, but its not in the interval
side effects	-

There doesn't seem to be much need for this kind of kline anymore,
and it is a bit of a CPU hog. The previous version of hybrid only supported
it if you didn't have KLINE_COMMENT_ONLY....
It still needs some rethinking/recoding.. but not today...
In most cases, its not needed, esp. on a busy server. don't
#define TIMED_KLINES, if you ban them on one server, they'll just
use another server. Just doesn't make sense to me any more.

-Dianora
*/

#ifdef TIMED_KLINES
/*
** check against a set of time intervals
*/

static int check_time_interval(char *interval)
{
  struct tm *tptr;
  time_t tick;
  char	 *p;
  int	 perm_min_hours, perm_min_minutes;
  int    perm_max_hours, perm_max_minutes;
  int    now, perm_min, perm_max;
  /*   char   reply[512];	*//* BLAH ! */

  tick = timeofday;
  tptr = localtime(&tick);
  now = tptr->tm_hour * 60 + tptr->tm_min;

  while (interval)
    {
      p = (char *)index(interval, ',');
      if (p)
	*p = '\0';
      if (sscanf(interval, "%2d%2d-%2d%2d",
		 &perm_min_hours, &perm_min_minutes,
		 &perm_max_hours, &perm_max_minutes) != 4)
	{
	  if (p)
	    *p = ',';
	  return(1);	/* Not an interval, let the caller deal with it */
	}
      if (p)
	*(p++) = ',';
      perm_min = 60 * perm_min_hours + perm_min_minutes;
      perm_max = 60 * perm_max_hours + perm_max_minutes;
      /*
      ** The following check allows intervals over midnight ...
      */
      if ((perm_min < perm_max)
	  ? (perm_min <= now && now <= perm_max)
	  : (perm_min <= now || now <= perm_max))
	{
	  /*
	  (void)ircsprintf(reply,
			   ":%%s %%d %%s :%s %d:%02d to %d:%02d.",
			   "You are not allowed to connect from",
			   perm_min_hours, perm_min_minutes,
			   perm_max_hours, perm_max_minutes);
			   */
	  return 1;

	  /*	  return(ERR_YOUREBANNEDCREEP); */
	}
      if ((perm_min < perm_max)
	  ? (perm_min <= now + 5 && now + 5 <= perm_max)
	  : (perm_min <= now + 5 || now + 5 <= perm_max))
	{
	  /*
	  (void)ircsprintf(reply, ":%%s %%d %%s :%d minute%s%s",
			   perm_min-now,(perm_min-now)>1?"s ":" ",
			   "and you will be denied for further access");
			   */
	  return 1;
	  /* return(ERR_YOUWILLBEBANNED); */
	}
      interval = p;
    }
  return 0;
}
#endif


/* 
D line hash table implementation 

Diane Bruce (Dianora/db)
*/

/*
clear_dline_hash_table

input		- NONE
output		- NONE
side effects 	- Initialize the dline hash table
*/

void clear_dline_hash_table()
{
  bzero((char *)dline_hash_table, sizeof(dline_hash_table));
}

/*
clear_dlines

input		- NONE
output		- NONE
side effects 	- clear the dline hash table
*/

static void clear_dlines()
{
  int i;
  DLINE_ENTRY *p;
  DLINE_ENTRY *next_p;

  for(i = 0; i < DLINE_HASH_SIZE; i++)
    {
      p = dline_hash_table[i];
      while(p)
	{
	  next_p = p->next;
	  if(p->conf_entry)
	    {
	      if(p->conf_entry->host)
		(void)free(p->conf_entry->host);
	      if(p->conf_entry->passwd)
		(void)free(p->conf_entry->passwd);
	      if(p->conf_entry->name)
		(void)free(p->conf_entry->name);
	      free_conf(p->conf_entry);
	    }
	  (void)free(p);
	  p = next_p;
	}
      dline_hash_table[i] = (DLINE_ENTRY *)NULL;
    }
}

/*
add_to_dline_hash()

inputs		- host_ip in =host= order
		- host_mask in =host= order
		- host ip banned as string
		- reason for ban
		
output		- none
side effects	-
*/

void add_to_dline_hash(unsigned long host_ip,
		       unsigned long host_mask,
		       char *host,
		       char *reason)
{
  aConfItem *aconf;
  DLINE_ENTRY *p;
  DLINE_ENTRY *new_entry;
  int i;

  aconf = make_conf();
  aconf->status = CONF_KILL;
  DupString(aconf->host,host);
  DupString(aconf->passwd,reason);
  aconf->name = (char *)NULL;
  aconf->port = 0;

  host_ip &= host_mask;

  new_entry = (DLINE_ENTRY *)MyMalloc(sizeof(DLINE_ENTRY));
  new_entry->ip = host_ip;
  new_entry->mask = host_mask;
  new_entry->conf_entry = aconf;
  new_entry->next = (DLINE_ENTRY *)NULL;

  i = hash_dline_ip(host_ip);

  if((p = dline_hash_table[i]) == (DLINE_ENTRY *)NULL)
    {
      dline_hash_table[i] = new_entry;
      return;
    }

  new_entry->next = p;
  dline_hash_table[i] = new_entry;
}

/*
find_host_in_dline_hash

inputs		- host ip in =network= order
output		- aConfItem if dlined NULL if not
side effects	- none
*/

aConfItem *find_host_in_dline_hash(unsigned long host_ip)
{
  int i;
  DLINE_ENTRY *p;
  unsigned long host_ip_host_order;

  host_ip_host_order = ntohl(host_ip);

  i = hash_dline_ip(host_ip_host_order);

  if((p = dline_hash_table[i]) == (DLINE_ENTRY *)NULL)
    return((aConfItem *)NULL);
  while(p)
    {
      if( (host_ip_host_order & p->mask) == p->ip)
	{
	  return(p->conf_entry);
	}
      p = p->next;
    }
  return((aConfItem *)NULL);
}

/*
hash_dline_ip()

input		- unsigned long ip address in host order
output		- integer value used as index into hash table
side effects	- hopefully, none
*/

static int hash_dline_ip(unsigned long ip)
{
  int hash;
  hash = ((ip >>= 12) + ip) & (DLINE_HASH_SIZE-1);
  return(hash);
}

/*
inputs		- hostname
		- pointer to ip_mask
output		- ip in host order
		- ip_mask pointed to by ip_mask_ptr is updated
side effects	- NONE
*/

unsigned long cidr_to_bitmask[]=
{
  /* 00 */ 0x00000000,
  /* 01 */ 0x80000000,
  /* 02 */ 0xC0000000,
  /* 03 */ 0xE0000000,
  /* 04 */ 0xF0000000,
  /* 05 */ 0xF8000000,
  /* 06 */ 0xFC000000,
  /* 07 */ 0xFE000000,
  /* 08 */ 0xFF000000,
  /* 09 */ 0xFF800000,
  /* 10 */ 0xFFC00000,
  /* 11 */ 0xFFE00000,
  /* 12 */ 0xFFF00000,
  /* 13 */ 0xFFF80000,
  /* 14 */ 0xFFFC0000,
  /* 15 */ 0xFFFE0000,
  /* 16 */ 0xFFFF0000,
  /* 17 */ 0xFFFF8000,
  /* 18 */ 0xFFFFC000,
  /* 19 */ 0xFFFFE000,
  /* 20 */ 0xFFFFF000,
  /* 21 */ 0xFFFFF800,
  /* 22 */ 0xFFFFFC00,
  /* 23 */ 0xFFFFFE00,
  /* 24 */ 0xFFFFFF00,
  /* 25 */ 0xFFFFFF80,
  /* 26 */ 0xFFFFFFC0,
  /* 27 */ 0xFFFFFFE0,
  /* 28 */ 0xFFFFFFF0,
  /* 29 */ 0xFFFFFFF8,
  /* 30 */ 0xFFFFFFFC,
  /* 31 */ 0xFFFFFFFE,
  /* 32 */ 0xFFFFFFFF
};

unsigned long host_name_to_ip(char *host,unsigned long *ip_mask_ptr)
{
  unsigned long generated_host_ip=0L;
  unsigned long current_ip=0L;
  unsigned int octet=0;
  int found_mask=0;

  while(*host)
    {
      if(isdigit(*host))
	{
	  octet *= 10;
	  octet += (*host & 0xF);
	}
      else if(*host == '.')
	{
	  current_ip <<= 8;
	  current_ip += octet;
	  octet = 0;
	}
      else if(*host == '/')
	{
	  found_mask = 1;
	  current_ip <<= 8;
	  current_ip += octet;
	  octet = 0;
	  generated_host_ip = current_ip;
	  current_ip = 0L;
	}

       if(*(host+1) == '\0')
	{
	  current_ip <<= 8;
	  current_ip += octet;
	  if(!found_mask)    
	    generated_host_ip = current_ip;
	}
      host++;
    }

  if(found_mask)
    {
      *ip_mask_ptr = cidr_to_bitmask[current_ip&0x1F];
    }
  else
    {
      *ip_mask_ptr = 0xFFFFFF00L;
    }
  return(generated_host_ip);
}


/*
report_dline_hash()

inputs		- aClient to source requesting stats D
output		-
side effects
*/

void report_dline_hash(aClient *sptr, int numeric)
{
  aConfItem *tmp;
  int i;
  DLINE_ENTRY *p;
  char *host,*pass;
  static  char	null[] = "<NULL>";

  for(i = 0; i < DLINE_HASH_SIZE; i++)
    {
      p = dline_hash_table[i];
      while(p)
	{
	  tmp = p->conf_entry;
	  if (tmp == (aConfItem *)NULL)
	    {
	      p = p->next;
	      continue;
	    }
	  host = BadPtr(tmp->host) ? null : tmp->host;
	  pass = BadPtr(tmp->passwd) ? null : tmp->passwd;

	  sendto_one(sptr, rpl_str(numeric), me.name,
		       sptr->name, 'D', host,
		       pass);
	  p = p->next;
	}
    }

}

/*
dhash_stats()

inputs		- 
output		-
side effects
*/

void dhash_stats(aClient *cptr, aClient *sptr,int parc, char *parv[],int out)
{
  aConfItem *tmp;
  int i;
  DLINE_ENTRY *p;
  int collision_count;
  char result_buf[256];

  if(out < 0)
    sendto_one(sptr,":%s NOTICE %s :*** hash stats for dhash",
	       me.name,cptr->name);
  else
    {
      (void)sprintf(result_buf,"*** hash stats for dhash\n");
      (void)write(out,result_buf,strlen(result_buf));
    }

  for(i = 0; i < DLINE_HASH_SIZE; i++)
    {
      p = dline_hash_table[i];

      collision_count = 0;
      while(p)
	{
	  tmp = p->conf_entry;
	  if (tmp == (aConfItem *)NULL)
	    {
	      collision_count++;
	      p = p->next;
	      continue;
	    }
          collision_count++;
	  p = p->next;
	}
      if(collision_count)
	{
	  if(out < 0)
	    sendto_one(sptr,":%s NOTICE %s :Entry %d (0x%X) Collisions %d",
		       me.name,cptr->name,i,i,collision_count);
	  else
	    {
	      (void)sprintf(result_buf,"Entry %d (0x%X) Collisions %d\n",
			    i,i,collision_count);
	      (void)write(out,result_buf,strlen(result_buf));
	    }
	}
    }
}

/* get_oper_privs
 *
 * inputs	- default privs
 * 		- privs as string
 * output	- default privs as modified by privs string
 * side effects -
 *
 * -Dianora
 */

int get_oper_privs(int int_privs,char *privs)
{
  Debug((DEBUG_DEBUG,"get_oper_privs called privs = [%s]",privs));

  while(*privs)
    {
      if(*privs == 'O')
	int_privs |= CONF_OPER_GLOBAL_KILL;
      else if(*privs == 'o')
	int_privs &= ~CONF_OPER_GLOBAL_KILL;
      else if(*privs == 'U')
	int_privs |= CONF_OPER_UNKLINE;
      else if(*privs == 'u')
	int_privs &= ~CONF_OPER_UNKLINE;
      else if(*privs == 'R')
	int_privs |= CONF_OPER_REMOTE;	/* squit/connect etc. */
      else if(*privs == 'r')
	int_privs &= ~CONF_OPER_REMOTE;	/* squit/connect etc. */
      else if(*privs == 'N')
	int_privs |= CONF_OPER_N;
      else if(*privs == 'n')
	int_privs &= ~CONF_OPER_N;
      else if(*privs == 'K')		/* kill and kline privs, 
					 * for monitor bots
					 */
	int_privs |= CONF_OPER_K;
      else if(*privs == 'k')
	int_privs &= ~CONF_OPER_K;
#ifdef GLINES
      else if(*privs == 'G')
	int_privs |= CONF_OPER_GLINE;
      else if(*privs == 'g')
	int_privs &= ~CONF_OPER_GLINE;
#endif
      privs++;
    }

  Debug((DEBUG_DEBUG,"about to return int_privs %x",int_privs));
  return(int_privs);
}

/*
 * oper_privs
 *
 * inputs	- pointer to cptr or NULL
 * output	- pointer to static string showing oper privs
 * side effects	-
 * return as string, the oper privs as derived from port
 * also, set the oper privs if given cptr is non NULL
 */

char *oper_privs(aClient *cptr,int port)
{
  static char privs_out[10];
  char *privs_ptr;

  privs_ptr = privs_out;
  *privs_ptr = '\0';

  if(port & CONF_OPER_GLINE)
    {
      if(cptr)
	SetOperGline(cptr);
      *privs_ptr++ = 'G';
    }
  else
    *privs_ptr++ = 'g';

  if(port & CONF_OPER_K)
    {
      if(cptr)
	SetOperK(cptr);

  /* Lets simply just not report it
   * as every oper will have default of 'K'
   * but monitor bots might only have 'k'
   */
  /*      *privs_ptr++ = 'K'; */

    }
  else
    *privs_ptr++ = 'k';

  if(port & CONF_OPER_N)
    {
      if(cptr)
	SetOperN(cptr);
      *privs_ptr++ = 'N';
    }

  if(port & CONF_OPER_GLOBAL_KILL)
    {
      if(cptr)
	SetOperGlobalKill(cptr);
      *privs_ptr++ = 'O';
    }
  else
    *privs_ptr++ = 'o';

  if(port & CONF_OPER_REMOTE)
    {
      if(cptr)
	SetOperRemote(cptr);
      *privs_ptr++ = 'R';
    }
  else
    *privs_ptr++ = 'r';
  
  if(port & CONF_OPER_UNKLINE)
    {
      if(cptr)
	SetOperUnkline(cptr);
      *privs_ptr++ = 'U';
    }
  else
    *privs_ptr++ = 'u';

  *privs_ptr = '\0';

  return(privs_out);
}
