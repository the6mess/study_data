#ifndef __XMODEM_H__
#define __XMODEM_H__

#include "stdlib.h"
#include "stdint.h"
#include "fsl_lpuart.h"
#include "fsl_clock.h"
#include "memory.h"
#include "property.h"
#include "bootloader_common.h"
#include "bl_reliable_update.h"
#include "crc32.h"


typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef int16_t  INT16;
typedef uint8_t  CHAR8;
typedef uint32_t UINT32;

//#define XMODEM_DEBUG
#define MILSEC_PER_SEC 1000


typedef struct
{
    UINT16   ucframeType;
    UINT16   ucframeNo;
    UINT16   ucCmpFrameNo;
    UINT8   xmdStat;/*0x1, drop one frame*/
    UINT16  usCrc;
    UINT8  *pFrameBuf;
} xmodemFrame;

typedef INT16 (*FUNCXPTR) (UINT8 *, UINT16);

INT16 xmodemUpload(void);
//INT16 xmodemGetCharInTime( UINT16 iOverTime, UINT8 *pucData );
INT16 SCI0_PollStrRx(CHAR8 *buff, UINT16 len, UINT32 type);
void RS232_BYTE_TX(UINT8 data);
void receive_byte(UINT8 data);

#ifdef ENABLE_XDOWNLOAD
INT16 xmodemDownload( void );
#endif

UINT16 getCrc8(UINT8 *pData, INT16 len);

/*old file 12.2.4*/
#if 0
typedef struct
{
	UINT8   ucframeType;
	UINT8   ucframeNo;
	UINT8   ucCmpFrameNo;
	UINT8   pucDataBuff[ 1024 ];
	UINT16  usCrc;
} xmodemFrame;

INT16 xmodemDownload( void );

INT16 xmodemUpload( void );
#endif
#endif

