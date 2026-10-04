/*****************************************************************************
** File Name:       socket_types.h         
** Author:          Yifeng.Wang            
** Date:            2009/07/22                                               
** Copyright:       2009 Spreadtrum, Incorporated. All Rights Reserved.      
** Description:     This file defines socket types and macro definitions
******************************************************************************
**                         Important Edit History                            *
** --------------------------------------------------------------------------*
** DATE             NAME                DESCRIPTION                          
** 2009/07/22       Yifeng.Wang         Create
******************************************************************************/

#ifndef  _SOCKET_TYPES_H_
#define  _SOCKET_TYPES_H_

/*---------------------------------------------------------------------------*
 *                          Include Files                                    *
 *---------------------------------------------------------------------------*/
#include "tcpip_types.h"
#include "os_api.h"

/*---------------------------------------------------------------------------*
 *                          Compiler Flag                                    *
 *---------------------------------------------------------------------------*/
#ifdef __cplusplus
    extern   "C"
    {
#endif
/*---------------------------------------------------------------------------*
 *                          MACRO DEFINITION                                 *
 *---------------------------------------------------------------------------*/
// Socket error
#define TCPIP_SOCKET_ERROR      (-1)      /* socket function error */
#define TCPIP_SOCKET_INVALID    (-1)      /* invalid socket */

// Socket Types
#define SOCK_STREAM     1           /* stream socket */
#define SOCK_DGRAM      2           /* datagram socket */
#define SOCK_RAW        3           /* raw-protocol interface */


// Async socket event Types
#define AS_NULL         0           /* async-select non event */
#define AS_READ         0x01        /* async-select read event */
#define AS_WRITE        0x02        /* async-select write event */
#define AS_CONNECT      0x04        /* async-select connect event */
#define AS_CLOSE        0x08        /* async-select peer close event */
#define AS_ACCEPT       0x10        /* async-select accept event */
#define AS_FULLCLOSED   0x20        /* async-select full closed event */
#define AS_READ_BUFFER_STATUS 0x40  /* async-select read buffer status report event,
                                     * this event is automatically set with AS_READ,
                                     * NOTE: set AS_READ_BUFFER_STATUS individually
                                     * will be no use! */


#define FULLCLOSED_FROM_TIMEWAIT 1  /* async-select full closed from the state TIME_WAIT */
#define FULLCLOSED_FROM_LASTACK  2  /* async-select full closed from the state LAST_ACK */
#define FULLCLOSED_FROM_RST      3  /* async-select full closed caused by RST */


// Socket option flags
#define SO_DEBUG        0x0001      /* turn on debugging info recording */
#define SO_ACCEPTCONN   0x0002      /* socket has had listen() */
#define SO_REUSEADDR    0x0004      /* allow local address reuse */
#define SO_KEEPALIVE    0x0008      /* keep connections alive */
#define SO_DONTROUTE    0x0010      /* just use interface addresses */
#define SO_BROADCAST    0x0020      /* permit sending of broadcast msgs */
#define SO_USELOOPBACK  0x0040      /* bypass hardware when possible */
#define SO_LINGER       0x0080      /* linger on close if data present */
#define SO_OOBINLINE    0x0100      /* leave received OOB data in line */
#define SO_TCPSACK      0x0200      /* Allow TCP SACK (Selective acknowledgment) */
#define SO_WINSCALE     0x0400      /* Set scaling window option */ 
#define SO_TIMESTAMP    0x0800      /* Set TCP timestamp option */ 
#define SO_BIGCWND      0x1000      /* Large initial TCP Congenstion window */ 
#define SO_HDRINCL      0x2000      /* user access to IP hdr for SOCK_RAW */
#define SO_NOSLOWSTART  0x4000      /* suppress slowstart on this socket */
#define SO_FULLMSS      0x8000      /* force packets to all be MAX size */


// for compatability with second-rate stacks/
#define SO_EXPEDITE     SO_NOSLOWSTART
#define SO_THROUGHPUT   SO_FULLMSS


// Additional options, not kept in so_options.
#define SO_SNDBUF       0x1001      /* send buffer size */
#define SO_RCVBUF       0x1002      /* receive buffer size */
#define SO_SNDLOWAT     0x1003      /* send low-water mark */
#define SO_RCVLOWAT     0x1004      /* receive low-water mark */
#define SO_SNDTIMEO     0x1005      /* send timeout */
#define SO_RCVTIMEO     0x1006      /* receive timeout */
#define SO_ERROR        0x1007      /* get error status and clear */
#define SO_TYPE         0x1008      /* get socket type */
#define SO_HOPCNT       0x1009      /* Hop count to get to dst   */
#define SO_MAXMSG       0x1010      /* get maximum message size, NOTE: it's only for
                                       message-oriented socket types (e.g. SOCK_DGRAM),
                                       no meaning for stream oriented sockets. */


// And some netport additions to setsockopt.
#define SO_RXDATA       0x1011      /* get count of bytes in sb_rcv */
#define SO_TXDATA       0x1012      /* get count of bytes in sb_snd */
#define SO_MYADDR       0x1013      /* return my IP address */
#define SO_NBIO         0x1014      /* set socket into NON-blocking mode */
#define SO_BIO          0x1015      /* set socket into blocking mode */
#define SO_NONBLOCK     0x1016      /* set/get blocking mode via optval param */
#define SO_CALLBACK     0x1017      /* set/get zero_copy callback routine */


/* TCP User-settable options (used with setsockopt).
 TCP-specific socket options use the 0x2000 number space. */
#define TCP_ACKDELAYTIME    0x2001  /* Set time for delayed acks */
#define TCP_NODELAY         0x2002  /* suppress delayed ACKs */
#define TCP_MAXSEG          0x2003  /* set TCP maximum segment size */
#define TCP_PEERACKED       0x2004  /* peer acked bytes in current TCP connection */

// Socket state bits
#define SS_NOFDREF          0x0001  /* no file table ref any more */
#define SS_ISCONNECTED      0x0002  /* socket connected to a peer */
#define SS_ISCONNECTING     0x0004  /* in process of connecting to peer */
#define SS_ISDISCONNECTING  0x0008  /*  in process  of disconnecting */
#define SS_CANTSENDMORE     0x0010  /* can't send more data to peer */
#define SS_CANTRCVMORE      0x0020  /* can't receive more data from peer */
#define SS_RCVATMARK        0x0040  /* at mark on input */
#define SS_PRIV             0x0080  /* privileged for broadcast, raw... */
#define SS_NBIO             0x0100  /* non-blocking ops */
#define SS_ASYNC            0x0200  /* async i/o notify */
#define SS_UPCALLED         0x0400  /* zerocopy data has been upcalled (for select) */
#define SS_INUPCALL         0x0800  /* inside zerocopy upcall (reentry guard) */
#define SS_UPCFIN           0x1000  /* inside zerocopy upcall (reentry guard) */
#define SS_WASCONNECTING    0x2000  /* SS_ISCONNECTING w/possible pending error */


// Address families.
#define AF_UNSPEC       0           /* unspecified */
#define AF_NS           1           /* local to host (pipes, portals) */
#define AF_INET         2           /* internetwork: UDP, TCP, etc. */
#define AF_MAX          (AF_INET +  1)


// socket flag
#define MSG_OOB         0x01        /* process out-of-band data */
#define MSG_PEEK        0x02        /* peek at incoming message */
#define MSG_DONTROUTE   0x04        /* send without using routing tables */
#define MSG_NEWPIPE     0x08        /* New pipe for recvfrom call   */
#define MSG_EOR         0x10        /* data completes record */
#define MSG_DONTWAIT    0x20        /* this message should be nonblocking */


// socket error code
#define ENOBUFS         1
#define ETIMEDOUT       2
#define EISCONN         3
#define EOPNOTSUPP      4
#define ECONNABORTED    5
#define EWOULDBLOCK     6
#define ECONNREFUSED    7
#define ECONNRESET      8
#define ENOTCONN        9
#define EALREADY        10
#define EINVAL          11
#define EMSGSIZE        12
#define EPIPE           13
#define EDESTADDRREQ    14
#define ESHUTDOWN       15
#define ENOPROTOOPT     16
#define EHAVEOOB        17
#define ENOMEM          18
#define EADDRNOTAVAIL   19
#define EADDRINUSE      20
#define EAFNOSUPPORT    21
#define EINPROGRESS     22
#define ELOWER          23          /* lower layer (IP) error */
#define ENOTSOCK        24          /* Includes sockets which closed while blocked */
#define EIEIO           27          /* bad input/output on Old Macdonald's farm :-) */
#define ETOOMANYREFS    28
#define EFAULT          29
#define ENETUNREACH     30


// Maximum queue length specifiable by listen.
#define SOMAXCONN       5


// socket shutdown options
#define SD_RECV         0
#define SD_SEND         1
#define SD_BOTH         2


/*---------------------------------------------------------------------------*
 *                          TYPE DEFINITION                                  *
 *---------------------------------------------------------------------------*/
/* socket types */
typedef int32 TCPIP_SOCKET_T;


/* Lightweight UDP receive upcall function pointer */
typedef int (*LWUDP_RECV_UPCALL_FPTR)(
    char* ptr, int len, void* label, uint16 srcport);


/* Berkeley style "Socket address" */
struct sci_sockaddr
{
    unsigned short  family;         /* address family */
    unsigned short  port;           /* port number */
    unsigned long   ip_addr;        /* ip address */  
    char            sa_data[8];     /* up to 14 bytes of direct address */
};


/* the definitions to support the select() function. These are about 
 * as UNIX-like as we can make 'em on embedded code. They are also 
 * fairly compatable with WinSock's select() definitions.
 */
typedef struct sci_fd_set   /* the select socket array manager */
{ 
    unsigned        fd_count;       /* how many are SET? */
    long            fd_array[12];   /* an array of SOCKETs, define FD_SETSIZE 12 tcpip internal definition */
} sci_fd_set;


/* Description of data base entry for a single host of dns  */
struct sci_hostent
{
    char*           h_name;         /* Official name of host. */
    char**          h_aliases;      /* Alias list. */
    int             h_addrtype;     /* Host address type. */
    int             h_length;       /* Length of address. */
    char**          h_addr_list;    /* List of addresses from name server. */
#define h_addr  h_addr_list[0] /* Address, for backward compatibility. */
};


/* Description of async socket event */
// read event
typedef struct  _SOCKET_READ_EVENT_IND_SIG
{
    _SIGNAL_VARS
    TCPIP_SOCKET_T  socket_id;  	
}SOCKET_READ_EVENT_IND_SIG_T;

// read buffer status event
typedef struct  _SOCKET_READ_BUFFER_STATUS_EVENT_IND_SIG
{
    _SIGNAL_VARS
    TCPIP_SOCKET_T  socket_id;
}SOCKET_READ_BUFFER_STATUS_EVENT_IND_SIG_T;

// write event
typedef struct  _SOCKET_WRITE_EVENT_IND_SIG
{
    _SIGNAL_VARS
    TCPIP_SOCKET_T  socket_id;  	
}SOCKET_WRITE_EVENT_IND_SIG_T;

// connect event
typedef struct  _SOCKET_CONNECT_EVENT_IND_SIG
{
    _SIGNAL_VARS
    TCPIP_SOCKET_T  socket_id;  	
    uint32          error_code;
}SOCKET_CONNECT_EVENT_IND_SIG_T;

// connection close event
typedef struct  _SOCKET_CONNECTION_CLOSE_EVENT_IND_SIG
{
    _SIGNAL_VARS
    TCPIP_SOCKET_T  socket_id;  	
    uint32          error_code;
}SOCKET_CONNECTION_CLOSE_EVENT_IND_SIG_T;

// accept event
typedef struct  _SOCKET_ACCEPT_EVENT_IND_SIG
{
    _SIGNAL_VARS
    TCPIP_SOCKET_T  socket_id;  	
}SOCKET_ACCEPT_EVENT_IND_SIG_T;

// connection close event
typedef struct  _SOCKET_FULL_CLOSED_EVENT_IND_SIG
{
    _SIGNAL_VARS
    TCPIP_SOCKET_T  socket_id;  	
    uint32          close_reason;
}SOCKET_FULL_CLOSED_EVENT_IND_SIG_T;

// struct needed by connection full closed event
typedef struct _SOCKET_CLOSED_PARA
{
    TCPIP_SOCKET_T  socket_id;
    uint32          asselect_event;
    uint32          task_id;
}SOCKET_CLOSED_PARA_T;

// struct asyncgethostbyname CNF
typedef struct  _ASYNC_GETHOSTBYNAME_CNF_SIG
{
    _SIGNAL_VARS
    int32               error_code;
    uint32              request_id;
    TCPIP_NETID_T       netid;
    struct sci_hostent  hostent;
}ASYNC_GETHOSTBYNAME_CNF_SIG_T;


/*---------------------------------------------------------------------------*
 *                          Compiler Flag                                    *
 *---------------------------------------------------------------------------*/
#ifdef   __cplusplus
    }
#endif

///////////////////////////////////////////////////////////////////////////////
#endif  /* _SOCKET_TYPES_H_ */
