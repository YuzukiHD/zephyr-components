/*****************************************************************************
** File Name:       socket_api.h         
** Author:          Yifeng.Wang            
** Date:            2009/07/22                                               
** Copyright:       2009 Spreadtrum, Incorporated. All Rights Reserved.      
** Description:     This file defines socket external interfaces
**                  Most of the content are copy from app_tcp_if.h
******************************************************************************
**                         Important Edit History                            *
** --------------------------------------------------------------------------*
** DATE             NAME                DESCRIPTION                          
** 2009/07/22       Yifeng.Wang         Create
** 2009/11/03       Yifeng.Wang         Amend for unified TCPIP interface
******************************************************************************/

#ifndef  _SOCKET_API_H_
#define  _SOCKET_API_H_

/*---------------------------------------------------------------------------*
 *                          Include Files                                    *
 *---------------------------------------------------------------------------*/
#include "app_tcp_if.h"

/*---------------------------------------------------------------------------*
 *                          Compiler Flag                                    *
 *---------------------------------------------------------------------------*/
#ifdef __cplusplus
    extern   "C"
    {
#endif
/*---------------------------------------------------------------------------*
 *                      FUNCTION DECLARATIONS                                *
 *---------------------------------------------------------------------------*/
/*****************************************************************************/
// Description: Parses a dotted-decimal Internet address
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: ip_str (IN) -- dotted-decimal Internet address 
//          PARAM2: ipaddr_ptr (OUT) -- pointer to buffer where parsed address is
//                  to be returned
//          RETURN: 1 - IP parsing is successful,
//                  0 - IP parsing failed.
/*****************************************************************************/
PUBLIC int inet_aton( char* ip_str, TCPIP_IPADDR_T* ipaddr_ptr );


/*****************************************************************************/
// Description: Formats an Internet address into its printable dotted-decimal
//              representation.
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: ipaddr (IN) -- Internet Address (in network order)
//          RETURN: pointer to null-terminated string containing dotted-decimal
//                  printable representation of input para 'in'.
/*****************************************************************************/
PUBLIC char* inet_ntoa( TCPIP_IPADDR_T ipaddr );


/*****************************************************************************/
// Description: Get socket total sent length and received length.
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: total_sent (OUT) -- pointer to total data length sent by sock. 
//                  If NULL, sent length will not return.
//          PARAM2: total_recv (OUT) -- pointer to total data length received by sock. 
//                  If NULL, recv length will not return.
//          RETURN: 0  if get socket data length succesfully;
//                  -1 if both params are NULL or fail to get length. 
/*****************************************************************************/
PUBLIC int sci_getsockthroughput( uint32* total_sent, uint32* total_recv );


/*****************************************************************************/
// Description: Get host name, the host name buffer should be 10 octets at least.
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: hostname (OUT) -- pointer of host name buffer, 
//          PARAM2: namelen  (IN) -- size of the host name buffer
//          RETURN: 0 if got host name succesfully; -1 if failed. 
/*****************************************************************************/
PUBLIC int sci_gethostname( char *hostname, int namelen );

/*****************************************************************************/
// Description: Set host name, the host name buffer should be 10 octets at least.
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: hostname (IN) -- pointer of host name buffer
//          RETURN: 0 - set host name succesfully; -1 - failed. 
/*****************************************************************************/
PUBLIC int sci_sethostname(const char *hostname);

/*****************************************************************************/
// Description: Get host address by its name(URL)
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: name_ptr (IN) -- pointer of host name, 
//          RETURN: pointer of the host address if succesful, else NULL.
//
//          "standard" Unixey version of sci_gethostbyname() that is not 
//          re-entrant. returns pointer to static hostent structure if 
//          successful, NULL if not successful. note that its not completely 
//          standard in that the list of aliases returned by the DNS lookup 
//          will not be in the hostent.
/*****************************************************************************/
PUBLIC struct sci_hostent * sci_gethostbyname( char *name_ptr );


/*****************************************************************************/
// Description: Get host address by its name(URL) with net ID
// Global resource : none
// Author : yifeng.wang
// Note :   PARAM1: name_ptr (IN) -- pointer of host name
//          PARAM2: netid (IN) -- net interface ID
//          RETURN: pointer of the host address if succesfully, else NULL.
//          when TCPIP support multi-bearers, param "netid" indicates
//          the net interface that DNS request will be sent through.
/*****************************************************************************/
PUBLIC struct sci_hostent * sci_gethostbyname_ext(
    char *name_ptr, TCPIP_NETID_T netid );


/*****************************************************************************/
// Description: asynchronous gethostbyname
// Global resource : s_request_id, s_gethostbynamelist_ptr
// Author : sally.he
// Note :   Resolve an text string address into an actual IP address.
//          Calls DNS if supported and necessary.
//          PARAM1: name_ptr (IN) -- name(URL) of the host to resolve
//          PARAM2: task_id (IN) -- task that to get host-parsing result event
//          PARAM3: time_out (IN) -- host-parsing time out value (unit: ms)
//          PARAM4: netid (IN) -- net interface ID
//          RETURN: async gethostbyname handle, none-0 if OK, 0 if error
/*****************************************************************************/
PUBLIC TCPIP_HOST_HANDLE sci_async_gethostbyname(
    char*           name_ptr,
    BLOCK_ID        task_id,
    uint32          time_out,
    TCPIP_NETID_T   netid );


/*****************************************************************************/
// Description: Get host IP address by its name
// Global resource : none
// Author : hyman.wu
// Note :   Resolve an text string address into an actual IP address. 
//          Calls DNS if supported and necessary.   
//          PARAM1: host (IN) -- textual host address
//          PARAM2: address (OUT) -- host IP address pointer
//          PARAM3: flags (IN) -- 1: block, 0: non-block
//          RETURN: 0 if address is get, else one of the ENP_ error codes
/*****************************************************************************/
PUBLIC int sci_parse_host(
    char*           host,    /* IN - textual IP address or host name */
    TCPIP_IPADDR_T* address, /* OUT - address if successful */
    int             flags ); /* IN - 1 for block, 0 for non-block */               


/*****************************************************************************/
// Description: Get host IP address by its name with net ID
// Global resource : none
// Author : yifeng.wang
// Note :   Resolve an text string address into an actual IP address. 
//          Calls DNS if supported and necessary.   
//          PARAM1: host (IN) -- textual host address, 
//          PARAM2: address (OUT) -- host IP address pointer
//          PARAM3: flags (IN) -- 1: block, 0: non-block
//          PARAM4: netid (IN) -- net interface ID
//          RETURN: 0 if address is get, else one of the ENP_ error codes
//          when TCPIP support multi-bearers, the param "netid" indicates
//          the net interface that DNS request will be sent through.
/*****************************************************************************/
PUBLIC int sci_parse_host_ext(
    char*           host,    /* IN - textual IP address or host name */
    TCPIP_IPADDR_T* address, /* OUT - address if successful */
    int             flags,   /* IN - 1 for block, 0 for non-block */ 
    TCPIP_NETID_T   netid ); /* IN - net interface ID */


/*****************************************************************************/
// Description: Get host IP address
// Global resource : none
// Author : hyman.wu
// Note :   RETURN: host IP address in network ending (Big-Ending)
//          in TCPIP support mutli-bearer, this function is the same as 
//          calling "sci_gethostaddress_ext(0)"
/*****************************************************************************/
PUBLIC TCPIP_IPADDR_T sci_gethostaddress( void );


/*****************************************************************************/
// Description: Get host IP address by net ID
// Global resource : none
// Author : yifeng.wang
// Note :   This function is used in multi-homing mode.   
//          PARAM1: netid (IN) -- net interface ID 
//          RETURN: host IP address in network ending (Big-Ending)
//          when TCPIP support multi-bearers, the param "netid" indicates
//          the net interface that the DNS request sent through
/*****************************************************************************/
PUBLIC TCPIP_IPADDR_T sci_gethostaddress_ext( TCPIP_NETID_T netid );


/*****************************************************************************/
// Description: Get port of the given socket
// Global resource : none
// Author : hyman.wu
// Note :   This function should be called after sci_sock_connect,
//          else it would be non-sense!   
//          PARAM1: so (IN) -- socket 
//          RETURN: if successful, return port value of socket so; else 0
/*****************************************************************************/
PUBLIC unsigned short sci_gethostport( TCPIP_SOCKET_T so );


/*****************************************************************************/
// Description: Lightweight UDP send for GPRS only
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: fhost (IN) -- foreign UDP IP address
//          PARAM2: fport (IN) -- foreign UDP port number
//          PARAM3: lport (IN) -- local UDP port number
//          PARAM4: data_ptr (IN) -- pointer to data being sent
//          PARAM5: data_len (IN) -- data length to send
//          RETURN: 0 if sent OK, else non-zero error code if error occured!
/*****************************************************************************/
PUBLIC int sci_udp_send(
    unsigned long   fhost,
    unsigned short  fport,
    unsigned short  lport,
    char*           data_ptr,
    int             data_len );


/*****************************************************************************/
// Description: Lightweight UDP send by net ID
// Global resource : none
// Author : yifeng.wang
// Note :   This function is only used in multi-homing mode
//          PARAM1: fhost (IN) -- foreign UDP IP address
//          PARAM2: fport (IN) -- foreign UDP port number
//          PARAM3: lport (IN) -- local UDP port number
//          PARAM4: data_ptr (IN) -- pointer to data being sent
//          PARAM5: data_len (IN) -- data length to send
//          PARAM6: netid (IN) -- net interface ID
//          RETURN: 0 if sent OK, else non-zero error code if error occured!
/*****************************************************************************/
PUBLIC int sci_udp_send_ext(
    unsigned long   fhost,
    unsigned short  fport,
    unsigned short  lport,
    char*           data_ptr,
    int             data_len,
    TCPIP_NETID_T   netid );


/*****************************************************************************/
// Description: Lightweight UDP open for GPRS only
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: fhost (IN) -- foreign UDP IP address
//          PARAM2: fport (IN) -- foreign UDP port number
//          PARAM3: lport_ptr (IN/OUT) -- pointer to local UDP port
//                  if lport_ptr pointed value is 0, a rand port will be set 
//                  and returned to this pointer!
//          PARAM4: handler (IN) -- function pointer to UDP data receive function
//                  function: (*handler)(char* ptr, int len, void* data)
//                  PARAM1: ptr -- recv data pointer
//                  PARAM2: len -- recv data length
//                  PARAM3: data -- UDP connection ID which set in PARAM5
//                  PARAM4: srcport -- source port of the UDP connection
//          PARAM5: label (IN) -- ID to be labeled to an UDP connection 
//          RETURN: NULL if UDP open fail, else non-zero pointer if OK!
/*****************************************************************************/
PUBLIC void* sci_udp_open(
    unsigned long           fhost,
    unsigned short          fport,
    unsigned short*         lport_ptr,
    LWUDP_RECV_UPCALL_FPTR  handler,
    void*                   label );
				 

/*****************************************************************************/
// Description: Lightweight UDP open by net ID
// Global resource : none
// Author : yifeng.wang
// Note :   This function is used in multi-homing mode.
//          PARAM1: fhost (IN) -- foreign UDP IP address
//          PARAM2: fport (IN) -- foreign UDP port number
//          PARAM3: lport_ptr (IN/OUT) -- pointer to local UDP port
//                  if lport_ptr pointed value is 0, then a rand port will be 
//                  set and return to this pointer
//          PARAM4: handler (IN) -- function pointer to UDP data receive function
//                  function: (*handler)(char* ptr, int len, void* data)
//                  PARAM1: ptr -- recv data pointer
//                  PARAM2: len -- recv data length
//                  PARAM3: data -- UDP connection ID which set in PARAM5
//                  PARAM4: srcport -- source port of the UDP connection
//          PARAM5: label (IN) -- ID to be labeled to an UDP connection
//          PARAM6: netid (IN) -- net interface ID
//          RETURN: NULL if UDP open fail, else non-zero pointer if OK!
/*****************************************************************************/
PUBLIC void* sci_udp_open_ext(
    unsigned long           fhost,
    unsigned short          fport,
    unsigned short*         lport_ptr,
    LWUDP_RECV_UPCALL_FPTR  handler,
    void*                   label,
    TCPIP_NETID_T           netid );


/*****************************************************************************/
// Description: Lightweight UDP close
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: con_ptr (IN) -- UDP connection pointer. It should be
//                  the same as the valu returned by sci_udp_open(_ext)()
/*****************************************************************************/
PUBLIC void sci_udp_close( void* con_ptr );


/*****************************************************************************/
// Description: create socket
// Global resource : none
// Author : hyman.wu, yifeng.wang
// Note :   PARAM1: family (IN) -- protocol family, 
//                  for TCP/UDP, this value should be AF_INET
//          PARAM2: type (IN) -- protocol type;
//                  for TCP, type is SOCK_STREAM; for UDP, type is SOCK_DGRAM
//          PARAM3: proto (IN) -- protocol name;
//                  for TCP/UDP, this can be default 0;
//          PARAM4: netid (IN) -- net interface ID;
//          RETURN: if successful, return an none-zero socket; else -1
//          net_id is used to indicate the net interface which the socket data
//          want to go through. Normally, net_id can be 0, then the socket will
//          use the first usable net interface to send/recv its data; 
//          this can cause problem in multi-homing mode, net_id can be get when
//          the network channel start up, e.g. in GPRS, it is PDP active!
/*****************************************************************************/
PUBLIC TCPIP_SOCKET_T sci_sock_socket(
    int family, int type, int proto, TCPIP_NETID_T netid );

/*****************************************************************************/
// Description: modify socket net ID
// Global resource : none
// Author : sally.he
// Note :   PARAM1: so (IN) -- socket
//          PARAM2: netid (IN) -- net interface ID
//          RETURN: if successful, return 0; else -1. 
//          NOTE: when socket is connectless or not in connect state, 
//                then netid modification on this socket is permitted, 
//                else function will return -1 with socket error EINVAL.
/*****************************************************************************/
PUBLIC int sci_sock_modifynetid( TCPIP_SOCKET_T so, TCPIP_NETID_T netid );

/*****************************************************************************/
// Description: get socket's state
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: so (IN) -- socket
//          PARAM2: state_ptr (OUT) -- pointer to socket state value
//          RETURN: 0 for success, -1 for failure
//          socket state value will be any combinations of STATE bit macros 
//          with SS_ type which defined in socket_types.h         
/*****************************************************************************/
PUBLIC int sci_sock_getsockstate( TCPIP_SOCKET_T so, short* state_ptr );


/*****************************************************************************/
// Description: socket bind 
// Global resource : none
// Author : hyman.wu
// Note :   bind socket with a given address (IP + port)
//          PARAM1: so (IN) -- socket
//          PARAM2: addr_ptr (IN) -- address to bound
//          PARAM3: addr_len (IN) -- address length
//                  this value is in fact no use, set with 0 
//          RETURN: 0 for success, -1 for failure        
/*****************************************************************************/
PUBLIC int sci_sock_bind(
    TCPIP_SOCKET_T so, struct sci_sockaddr* addr_ptr, int addr_len );


/*****************************************************************************/
// Description: socket listen 
// Global resource : none
// Author : hyman.wu
// Note :   socket listen, for TCP server to start listen connection from client
//          PARAM1: so (IN) -- socket
//          PARAM2: backlog (IN) -- maximum connecting num from client
//                  backlog can be set maximum as SOMAXCONN, backlog means
//                  maximum socket numbers in connecting( TCP handshake ),
//                  backlog doesn't include socket connected!
//          RETURN: 0 for success, -1 for failure        
/*****************************************************************************/
PUBLIC int sci_sock_listen( TCPIP_SOCKET_T so, int backlog );


/*****************************************************************************/
// Description: socket accept 
// Global resource : none
// Author : hyman.wu
// Note :   socket accept, for TCP server to accept new connection from client
//          sci_sock_listen() should be called at first! 
//          PARAM1: so (IN) -- socket for listening
//          PARAM2: addr_ptr (OUT) -- new connection address from client
//          PARAM3: addr_len (IN) -- address length
//                  this value is in fact no use, set with 0 
//          RETURN: success: socket of the new accepted connection; failure: -1       
/*****************************************************************************/
PUBLIC TCPIP_SOCKET_T sci_sock_accept(
    TCPIP_SOCKET_T so, struct sci_sockaddr* addr_ptr, int* addr_len );


/*****************************************************************************/
// Description: socket connect 
// Global resource : none
// Author : hyman.wu
// Note :   socket connect, for TCP client to connect to server,
//          PARAM1: so (IN) -- socket
//          PARAM2: addr_ptr (IN) -- address to connect
//          PARAM3: addr_len (IN) -- address length
//                  this value is in fact no use, set with 0 
//          RETURN: 0 for success, -1 for failure        
/*****************************************************************************/
PUBLIC int sci_sock_connect(
    TCPIP_SOCKET_T so, struct sci_sockaddr* addr_ptr, int addr_len );


/*****************************************************************************/
// Description: get socket's peer address 
// Global resource : none
// Author : hyman.wu
// Note :   This function is useful to SOCK_STREAM type (e.g. TCP) socket
//          which is connected to peer.
//          PARAM1: so (IN) -- socket
//          PARAM2: addr_ptr (OUT) -- peer address pointer
//          RETURN: 0 for success, -1 for failure
/*****************************************************************************/
PUBLIC int sci_sock_getpeername(
    TCPIP_SOCKET_T so, struct sci_sockaddr* addr_ptr );


/*****************************************************************************/
// Description: get socket's local address 
// Global resource : none
// Author : hyman.wu
// Note :   This function is useful to SOCK_STREAM type (e.g. TCP) socket.
//          PARAM1: so (IN) -- socket
//          PARAM2: addr_ptr (OUT) -- local address pointer
//          RETURN: 0 for success, -1 for failure
/*****************************************************************************/
PUBLIC int sci_sock_getsockname(
    TCPIP_SOCKET_T so, struct sci_sockaddr* addr_ptr );


/*****************************************************************************/
// Description: set socket options
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: so (IN) -- socket
//          PARAM2: name (IN) -- option name
//                  macros are defined with prefix SO_ in socket_types.h 
//          PARAM3: arg_ptr (IN) -- option value pointer
//                  for sock options without arg (e.g. SO_NBIO), set to NULL
//          RETURN: 0 for success, -1 for failure        
/*****************************************************************************/
PUBLIC int sci_sock_setsockopt( TCPIP_SOCKET_T so, int name, void* arg_ptr );


/*****************************************************************************/
// Description: get socket options
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: so (IN) -- socket
//          PARAM2: name (IN) -- option name
//                  macros are defined with prefix SO_ in socket_types.h 
//          PARAM3: arg_ptr (OUT) -- option value pointer
//          RETURN: 0 for success, -1 for failure
/*****************************************************************************/
PUBLIC int sci_sock_getsockopt( TCPIP_SOCKET_T so, int name, void* arg_ptr );


/*****************************************************************************/
// Description: socket receive ( for TCP )
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: so (IN) -- socket
//          PARAM2: buf (OUT) -- receive buffer pointer
//          PARAM3: len (IN) -- receive buffer length
//          PARAM4: flag (IN) -- socket receive option, default with 0
//          RETURN: > 0, data length received;
//                  = 0, connection is closed;
//                  = -1, function error
/*****************************************************************************/
PUBLIC int sci_sock_recv( TCPIP_SOCKET_T so, char* buf, int len, int flag );


/*****************************************************************************/
// Description: socket receive ( for UDP )
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: so (IN) -- socket
//          PARAM2: buf (OUT) -- receive buffer pointer
//          PARAM3: len (IN) -- receive buffer length
//          PARAM4: flag (IN) -- socket receive option, default with 0
//          PARAM5: from (OUT) -- data source address pointer
//          RETURN: > 0, data length received;
//                  = -1, function error
/*****************************************************************************/
PUBLIC int sci_sock_recvfrom(
    TCPIP_SOCKET_T so, char* buf, int len, int flag, struct sci_sockaddr* from );


/*****************************************************************************/
// Description: socket send ( for TCP )
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: so (IN) -- socket
//          PARAM2: buf (IN) -- send data pointer
//          PARAM3: len (IN) -- send data length
//          PARAM4: flag (IN) -- socket send option, default with 0
//          RETURN: if success, return data length sent; else -1 for failure
/*****************************************************************************/
PUBLIC int sci_sock_send( TCPIP_SOCKET_T so, char* buf, int len, int flags );


/*****************************************************************************/
// Description: socket send ( for UDP )
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: so (IN) -- socket
//          PARAM2: buf (IN) -- send data pointer
//          PARAM3: len (IN) -- send length
//          PARAM4: flag (IN) -- socket send option, default with 0
//          PARAM5: to (IN) -- data destination address pointer
//          RETURN: if success, return data length sent; else -1 for failure
/*****************************************************************************/
PUBLIC int sci_sock_sendto(
    TCPIP_SOCKET_T so, char* buf, int len, int flag, struct sci_sockaddr* to );


/*****************************************************************************/
// Description: socket shutdown ( for TCP )
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: so (IN) -- socket
//          PARAM2: how (IN) -- shutdown type, include
//                  SD_RECV, SD_SEND and SD_BOTH, defined in socket_types.h
//          RETURN: 0 for success, -1 for failure
/*****************************************************************************/
PUBLIC int sci_sock_shutdown( TCPIP_SOCKET_T so, int how );


/*****************************************************************************/
// Description: socket select
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: in (IN) -- socket list for read event watching, 
//          PARAM2: out (IN) -- socket list for write event watching,
//          PARAM3: ex (IN) -- socket list for exception event watching,
//          PARAM4: tv (IN) -- watching time-out value, unit: 0.1s
//          RETURN: > 0: watching event occured;
//                  = 0: watching event time-out;
//                  =-1: failure, socket is invalid
/*****************************************************************************/
PUBLIC int sci_sock_select(
            sci_fd_set* in,  
            sci_fd_set* out,
            sci_fd_set* ex,
            long tv );


/*****************************************************************************/
// Description: clear socket from socket list
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: so (IN) -- socket
//          PARAM2: set (IN) -- socket list pointer
/*****************************************************************************/
PUBLIC void SCI_FD_CLR( TCPIP_SOCKET_T so, sci_fd_set* set );


/*****************************************************************************/
// Description: add socket to socket list
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: so (IN) -- socket
//          PARAM2: set (IN) -- socket list pointer
/*****************************************************************************/
PUBLIC void SCI_FD_SET( TCPIP_SOCKET_T so, sci_fd_set* set );


/*****************************************************************************/
// Description: check if socket in socket list
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: so (IN) -- socket
//          PARAM2: set (IN) -- socket list pointer
//          RETURN: 1 - set; 0 - not set
/*****************************************************************************/
PUBLIC int SCI_FD_ISSET( TCPIP_SOCKET_T so, sci_fd_set* set );


/*****************************************************************************/
// Description: clear socket list
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: set (IN) -- socket list,
/*****************************************************************************/
PUBLIC void SCI_FD_ZERO( sci_fd_set* set );


/*****************************************************************************/
// Description: socket close
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: so (IN) -- socket
//          RETURN: 0 for success, -1 for failure
/*****************************************************************************/
PUBLIC int sci_sock_socketclose( TCPIP_SOCKET_T so );


/*****************************************************************************/
// Description: get last error of a given socket
// Global resource : none
// Author : hyman.wu
// Note :   PARAM1: so (IN) -- socket
//          RETURN: error number of input socket
//                  error number definition are defined as E** in socket_types.h
//          Return value will be undefined if the socket has not previously 
//          reported an error.
/*****************************************************************************/
PUBLIC int sci_sock_errno( TCPIP_SOCKET_T so );


/*****************************************************************************/
// Description: async socket select
// Global resource : none
// Author : kelly.li
// Note :   Call async select to set watch for given socket, when event occurs,
//          TCPIP will send message to the given task to inform the event,
//          Repeatly function call on same socket will cover events set before!
//          PARAM1: so (IN) -- socket
//          PARAM2: task_id (IN) -- task ID which is to deal with the socket event
//          PARAM3: event (IN) -- socket event to be watched, 
//                  the events are defined as AS_ in socket_types.h
//          RETURN: 0 - success, -1 - error
/*****************************************************************************/
PUBLIC int sci_sock_asyncselect(
    TCPIP_SOCKET_T so, BLOCK_ID task_id, uint32 event);


/*****************************************************************************/
// Description: async socket receive ( for TCP )
// Global resource : none
// Author : yifeng.wang
// Note :   compared with sci_sock_recv(), it can indicate how many data left 
//          in socket receive buffer after current call! We recommend the user 
//          call this function on socket that being watched under async select!
//          PARAM1: so (IN) -- socket
//          PARAM2: buf (OUT) -- receive buffer pointer
//          PARAM3: len (IN) -- receive buffer length
//          PARAM4: flag (IN) -- socket receive option, default with 0
//          PARAM5: left_len_ptr (OUT) -- data size left in socket buffer
//                  if input is NULL, then it's same as sci_sock_recv
//          RETURN: > 0, data length received;
//                  = 0, connection is closed;
//                  = -1, function error        
/*****************************************************************************/
PUBLIC int sci_sock_asyncrecv(
    TCPIP_SOCKET_T  so,
    char*           buf,
    int             len, 
    int             flag, 
    int*            left_len_ptr);


/*****************************************************************************/
// Description: async socket receive ( for UDP )
// Global resource : none
// Author : yifeng.wang
// Note :   compared with sci_sock_recvfrom(), it can indicate how many data left 
//          in socket receive buffer after current call! We recommend the user 
//          call this function on socket that being watched under async select!
//          PARAM1: so (IN) -- socket
//          PARAM2: buf (OUT) -- receive buffer pointer
//          PARAM3: len (IN) -- receive buffer length
//          PARAM4: flag (IN) -- socket receive option, default with 0
//          PARAM5: from (OUT) -- data source address pointer
//          PARAM6: left_len_ptr (OUT) -- data size left in socket buffer
//                  if input is NULL, then it's same as sci_sock_recv
//          RETURN: > 0, data length received;
//                  = -1, function error
/*****************************************************************************/
PUBLIC int sci_sock_asyncrecvfrom(
    TCPIP_SOCKET_T  so, 
    char*           buf, 
    int             len,
    int             flag,
    struct sci_sockaddr* from, 
    int*            left_len_ptr);


/*****************************************************************************/
// Description : get socket's throughput by socket id
// Global resource : none
// Author : lisa.lin
// Note :   PARAM1: so (IN) -- socket
//          PARAM2: tot_sent (OUT) -- total sent length pointer
//          PARAM3: tot_recv (OUT) -- total recv length pointer
//          RETURN: 0 for success, -1 for failure
/*****************************************************************************/
PUBLIC int sci_sock_getthroughput( TCPIP_SOCKET_T so, uint32* total_sent, uint32* total_recv );


/*---------------------------------------------------------------------------*
 *                          Compiler Flag                                    *
 *---------------------------------------------------------------------------*/
#ifdef   __cplusplus
    }
#endif

///////////////////////////////////////////////////////////////////////////////
#endif  /* _SOCKET_API_H_ */
