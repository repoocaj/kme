#define KME_H_VERSION	"@Id$"

/************************************************************************
 * Ioctl command arguments for DIGI parameters.
 ************************************************************************/

#define DIGI_KME	(('e'<<8) | 98)		/* Read/Write Host	*/
						/* Adapter Memory	*/

/************************************************************************
 * Digiboard KME definitions and structures.
 ************************************************************************/

#define	RW_IDLE		0	/* Operation complete */
#define	RW_READ		1	/* Read Memory */
#define	RW_WRITE	2	/* Write Memory	*/

typedef struct
{
	unsigned char	rw_req;		/* Request type			*/
	unsigned char	rw_board;	/* Board/Adapter number		*/
	unsigned char	rw_module;	/* Module number		*/
	unsigned char	rw_reserved;	/* Reserved for expansion	*/
	unsigned long	rw_addr;	/* Address in concentrator	*/
	unsigned short	rw_size;	/* Read/write request length	*/
	unsigned char	rw_data[128];	/* Data to read/write		*/
} rw_t;


/********************************************************************
 * Recommended (but not officially assigned) port number for
 * KME requests to read/write memory.
 *
 * This number can be overridden with the "-U" parameter,
 * or with a "kme/udp" entry in the /etc/services file.
 ********************************************************************/

#define UDP_PORT	2773		/* Default UDP port */
