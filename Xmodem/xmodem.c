/**********************************************************************
 daijisheng@teg, copyright 2008-2010
 xmodem.c
 用于实现串口XMODEM / 1K XMODEM 协议上载文件的功能
 函数:
        INT16 xmodemUpload( CHAR8 *fileName)()
        XMODEM上载文件到目标板的入口函数
        fileName为上载到目标板的文件名，默认为"/tffs0/xmodemFile.dat"
              
        INT16 xmodemDownLoad(CHAR8 *fileName)()
        XMODEM下载文件到PC 机的入口函数
              
 调用:
 被调用:
 		NULL
 **********************************************************************/
#include "xmodem.h"


#define DEMO_LPUART          LPUART1

extern volatile CHAR8 SCI0_rxbuf;
//extern volatile INT16 Sci0RxNum;
CHAR8 xmodemDataBuf[133*2];
volatile UINT16 Index = 0;



#define MAX_RETRY_TIMES_FOR_STARTUP   120   /*开始传送最大的等待次数*/
#define MAX_RETRY_TIMES               60   /*接收开始标志，序号及序号补码时最大等待次数*/
#define WAIT_FRAME_TIME               1    /*接收帧的超时时间为1s*/       
#define WAIT_DATA_TIME                1    /*接收帧序号及数据区的超时时间为1s*/  
#define SOH_DATA_LENGTH               128
#define STX_DATA_LENGTH               1024
#define SIZE                 ((UINT16)133)

/*  the control character in Xmodem  */
#define  SOH       ( 0x01 )
#define  STX       ( 0x02 )
#define  EOT       ( 0x04 )
#define  ACK       ( 0x06 )
#define  DLE       ( 0x10 )
#define  XON       ( 0x11 )
#define  XOFF      ( 0x13 )
#define  NAK       ( 0x15 )
#define  SYN       ( 0x16 )
#define  CAN       ( 0x18 )
#define  OUTC      ( 0x43 )

#define XMODEM_CRC_SIZE        2    /* Crc_High Byte + Crc_Low Byte */
#define XMODEM_FRAME_ID_SIZE   2    /* Frame_Id + 255-Frame_Id */
#define XMODEM_DATA_SIZE_SOH   128  /* for Xmodem protocol */
#define XMODEM_DATA_SIZE_STX   1024 /* for 1K xmodem protocol */
#define USE_1K_XMODEM          0    /* 1 for use 1k_xmodem 0 for xmodem */
#define FLASH_PAGE_SIZE       (256) /* size of flash page */
#define FLASH_SECTOR_SIZE     (0X1000) /* size of flash sector */
#define sencondary_start_address (0x60240000) /* start address of sencondary region */


#define ERROR ((INT16)1)
#define OK    ((INT16)(0))
//#ifdef ERROR
//#undef ERROR
//#define ERROR -1
//#endif

enum{
	  Head,
      FrameType,
	  FrameId,
	  FrameCmId,
	  Data
};

#if (USE_1K_XMODEM)
 #define XMODEM_DATA_SIZE  XMODEM_DATA_SIZE_STX
 #define XMODEM_HEAD       STX
#else
 #define XMODEM_DATA_SIZE  XMODEM_DATA_SIZE_SOH
 #define XMODEM_HEAD       SOH
#endif

static xmodemFrame  gstrXmodemFrame;

void xmodemTransCancel( void );


///* 超时接收字符 */
//INT16 xmodemGetCharInTime( UINT16 iOverTime, UINT8 *pucData )
//{
//	UINT16 i,j;

//	for (i=0;i<iOverTime;i++)
//	{
//		for (j=0;j<50000;j++) 
//		{ 
//			if (SCI0SR1_RDRF == 1)
//			{
//				*pucData = SCI0DRL;
//				if (SCI0SR1_OR == 1) /*clear Overrun Flag*/
//				{
//					asm ldaa  SCI0DRL;
//					#ifdef XMODEM_DEBUG
//					logAdd( "\n[xmodemGetCharInTime]:Overrun\n" ,0);
//					#endif
//  				}
//				return OK;
//			}
//			micSecDelay(20);
//			//if ((j&0x3FF) == 0)
//		}
//	}
//	return ERROR;
//}


INT16 SCI0_PollStrRx(CHAR8 *buff, UINT16 len, UINT32 type)
{  
   UINT16 length = 0;
   switch(type)
   {
      case Head:
	  {
	   *buff = xmodemDataBuf[0];
	   length = len;
	   return length;
	  }
      case FrameType:
	  {
		*buff = xmodemDataBuf[0];
	    length = len;
	    return length;
	  }
	  case FrameId:
	  {
		*buff = xmodemDataBuf[1];
	    length = len;
	    return length;
	  }
	  case FrameCmId:
	  {
		*buff = xmodemDataBuf[2];
	    length = len;
	    return length;
	  }
	  case Data:
	  {
		UINT32 i =0;
		while(i < len)
		{
		  buff[i] = xmodemDataBuf[3+i];
		  i++;
		}
		
	    length = len;
	    return length;
	  }
	  default:
      break;
   }
   
  return length;

}

void RS232_BYTE_TX(UINT8 data)
{
   if(kLPUART_TxDataRegEmptyFlag & LPUART_GetStatusFlags(DEMO_LPUART))
   LPUART_WriteByte(DEMO_LPUART, data);
   SDK_DelayAtLeastUs(1000*50, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
}

void receive_byte(UINT8 data)
{
   xmodemDataBuf[Index] = data;
   Index++;
        
   if(Index >= SIZE)
   Index = 0;
        
}

/* 获取8位CRC校验 值*/
UINT16 getCrc8( UINT8 *pData, INT16 len )
{
	INT16  i, j;
	UINT16 usCrc = 0;
	
	for( i = 0; i < len; i++ )
    {
        usCrc = usCrc ^ pData[i] << 8;
        for( j = 0; j < 8; ++j )
        {
            if( usCrc & 0x8000 )
            {
                usCrc = usCrc << 1 ^ 0x1021;
            }
            else
            {
                usCrc = usCrc << 1;
            }
        }
    }
    //usCrc &= 0xffff;
    return( usCrc );        
}

#if 0
/* 把接收的文件拷贝入指定的文件*/
static INT16 xmodemCopyDataToFile( UINT8 *pucDataBuff, INT16 iBuffLength )
{
	static UINT8 * dataptr=NULL;
	static INT16 totalNum = 0;
	static UINT16 page = 0;
	static UINT16 addr = 0;

  //spi_writeflash (page, addr, pucDataBuff, (UINT32)iBuffLength);
  if (addr + iBuffLength >= 264) 
  {
      addr =  addr + iBuffLength - 264;
      page++;
  } 
  else
   addr +=iBuffLength;
  return iBuffLength;
	/*if (dataptr == NULL)
		dataptr = &xmodemDataBuf[0];
	if( ( pucDataBuff == NULL ) || ( iBuffLength <= 0 ) )
	{
		//logAdd( "[xmodemCopyDataToFile]:the para is invalid\n\r" );
		return( -1 );
	}
	
	//return( write( gRecvFileId, pucDataBuff, iBuffLength ) );
	if (totalNum<0)
	{
	memcpy (dataptr, pucDataBuff, iBuffLength);
	dataptr+=iBuffLength;
	totalNum+=iBuffLength;
	} 
	else
	  micSecDelay (1);
	return iBuffLength;*/
}
#endif

/* 接收串口帧启动函数 */	
UINT16 xmodemStartup( void )
{
	INT16           i;
	UINT8 ucFrameType;
	
	for( i = 0; i < MAX_RETRY_TIMES_FOR_STARTUP; i++ ) /*最尝试120次，1次约1s*/
	{
		RS232_BYTE_TX( OUTC );/*OUTC:enable CRC, NAK:enable check sum,发送字符C，等待上位机回应*/
		
		if (SCI0_PollStrRx ((CHAR8 *)&ucFrameType, 1, FrameType) == 0)/*接收一个字符，超时时间1s*/
		//if( xmodemGetCharInTime( WAIT_FRAME_TIME, &ucFrameType ) == ERROR )
		{
			continue;
		}
    	
		switch( ucFrameType ) /*应答帧类型判断*/
		{
			case SOH:
                            gstrXmodemFrame.ucframeType = ucFrameType;
                            gstrXmodemFrame.pFrameBuf= (UINT8 *) malloc (SOH_DATA_LENGTH+2);
                            if (gstrXmodemFrame.pFrameBuf == NULL)
                            {
                                return ERROR;
                            }
                            
                            #ifdef XMODEM_DEBUG
				 logAdd( "[xmodemStartup]:%d xmodem startup succeed\n", i);
				 #endif
                            return OK;
			case STX:
				 gstrXmodemFrame.ucframeType = ucFrameType;
				
                            gstrXmodemFrame.pFrameBuf = (UINT8 *) malloc (STX_DATA_LENGTH+2);
                            if (gstrXmodemFrame.pFrameBuf == NULL)
                            {
                                return ERROR;
                            }
                            #ifdef XMODEM_DEBUG
				 logAdd( "[xmodemStartup]:%d xmodem startup succeed\n", i);
				 #endif
    			        return OK;
			case 0x3:
				return ERROR;
			default:
				#ifdef XMODEM_DEBUG
				logAdd( "[xmodemStartup]:xmodem receive 0x%x \n",ucFrameType );
				#endif
				break;
		}
	}
	#ifdef XMODEM_DEBUG
	logAdd( "\n[xmodemStartup]:overtime\n" ,0);
	#endif
	xmodemTransCancel( );
	return ERROR;
}


/*接收帧的开始字节*/
static INT16 xmodemGetHead( void )
{
	UINT8 ucHead;
	UINT16   i;
	
	for( i = 0; i < MAX_RETRY_TIMES; i++ )
	{
		if (SCI0_PollStrRx ((CHAR8 *)&ucHead, 1, Head) == 0 )
		//if(xmodemGetCharInTime( WAIT_FRAME_TIME, &ucHead ) == ERROR)
		{
			RS232_BYTE_TX( NAK );
			continue;
		}
		switch( ucHead )
		{
			case SOH:
			case STX:
				gstrXmodemFrame.ucframeType = ucHead;
				return( 0 );
			case EOT:
				RS232_BYTE_TX( ACK );    		
				return( EOT );
			case CAN:
				return( CAN );
			default:
				#ifdef XMODEM_DEBUG
				logAdd( "[xmodemGetHead]:get 0x%x\n",ucHead);
				#endif
				RS232_BYTE_TX( NAK );
				break;
		}
	}

	#ifdef XMODEM_DEBUG
	logAdd( "[xmodemGetHead]:overtime\n",0);
	#endif
	xmodemTransCancel();
	return (-1);
}
	

/*接收帧的序号ucFrameId以及序号补码ucCmpFrameId*/	
static INT16 xmodemGetFrameId( void )
{
	UINT8 ucFrameId=0;
	UINT8 ucCmpFrameId=0;
	static INT16  iRetryTime = 0;
	
	while( iRetryTime < MAX_RETRY_TIMES )
	{
		if (SCI0_PollStrRx ((CHAR8 *)&ucFrameId, 1, FrameId) == 0)
		//if( xmodemGetCharInTime( WAIT_DATA_TIME, &ucFrameId ) == ERROR )
		{
			iRetryTime++;
			continue;
		}
		iRetryTime = 0;
		break;
	}
	while( iRetryTime < MAX_RETRY_TIMES )
	{
		if (SCI0_PollStrRx ((CHAR8 *)&ucCmpFrameId, 1, FrameCmId) == 0)
		//if( xmodemGetCharInTime( WAIT_DATA_TIME, &ucCmpFrameId ) == ERROR )
		{
			iRetryTime++;
			continue;
		}
		iRetryTime = 0;
		break;
	}
    
	if( ucFrameId != ( 0xff - ucCmpFrameId ) )
	{
		iRetryTime++;
		if( iRetryTime >= MAX_RETRY_TIMES )
		{
			#ifdef XMODEM_DEBUG
			logAdd( "[xmodemGetFrameId]:Recv frame ID is overtime\n",0 );
			return( -1 );
			#endif
		}
		RS232_BYTE_TX( NAK ); /*请求重传*/
		return( NAK );
	}
    
	if( gstrXmodemFrame.ucframeNo != ucFrameId ) 
	{
		#ifdef XMODEM_DEBUG
		//logAdd( "[xmodemGetFrameId]:frame ID is not expectation. except %d, ", gstrXmodemFrame.ucframeNo );
        	//logAdd( "get %d\n", ucFrameId);
		#endif
		if(gstrXmodemFrame.ucframeNo == (ucFrameId + 1)) /*包序号不连续*/
		{
			gstrXmodemFrame.xmdStat = 0x1; /*设置丢弃标志*/
		}
		else
		{
			xmodemTransCancel( );
			return( -1 );
		}
	}
      iRetryTime = 0;
	return( 0 );   
}


/*接收数据区128个字节以及CRC校验码*/
static INT16 xmodemGetData( )
{
	INT16            i,j;
	UINT16 dataLen = STX_DATA_LENGTH;
	UINT16 usCrc = 0;
	//UINT8 * pucGetDataBuff;
	static INT16     iRetryTime = 0;
	
	if( gstrXmodemFrame.ucframeType == SOH )
	{
		dataLen = SOH_DATA_LENGTH;
		/*pucGetDataBuff = (UINT8 *) malloc (SOH_DATA_LENGTH+2);
		if (pucGetDataBuff == NULL)
			return (-1);*/
	}
	else if( gstrXmodemFrame.ucframeType == STX )
	{
		dataLen = STX_DATA_LENGTH;
		/*pucGetDataBuff = (UINT8 *) malloc (STX_DATA_LENGTH+2);
		if (pucGetDataBuff == NULL)
			return (-1);*/
	}
	
	for( i = dataLen + 2; i > 0; i-=j)
	{
		/*if (SCI0_PollStrRx (&pucGetDataBuff[i], 1, WAIT_DATA_TIME*MILSEC_PER_SEC) == 0)
		//if( xmodemGetCharInTime( WAIT_DATA_TIME, &pucGetDataBuff[ i ] ) == ERROR )
		{
			if( iRetryTime == 10 )
			{
				#ifdef XMODEM_DEBUG
				logAdd( "[xmodemGetData]:xmodem Get SOH data overtime\n",0 );
				#endif
				xmodemTransCancel( );
				free(pucGetDataBuff);
				return( -1 );
			}
			iRetryTime++;
		}*/
		
		j = SCI0_PollStrRx ((CHAR8 *)&gstrXmodemFrame.pFrameBuf[dataLen + 2 - i], i, Data);
		if (j < i)
		{
			if( iRetryTime >= MAX_RETRY_TIMES )
			{
				#ifdef XMODEM_DEBUG
				logAdd( "[xmodemGetData]:xmodem Get data overtime\n",0 );
				#endif
				xmodemTransCancel( );
				return( -1 );
			}
                    #ifdef XMODEM_DEBUG
			logAdd( "[xmodemGetData]:Get data overtime %d.\n",j );
			#endif
			iRetryTime++;
                    RS232_BYTE_TX( NAK );
        		return( NAK );
		}
		else
		{
			break;
		}
	}

    if(gstrXmodemFrame.xmdStat == 0x1)
    {
        gstrXmodemFrame.xmdStat = 0;
        gstrXmodemFrame.ucframeNo--;
        iRetryTime = 0;
        #ifdef XMODEM_DEBUG
	logAdd( "[xmodemGetData]:drop one frame\n", 0);
	#endif
        return 0;
    }
       /*帧数据crc校验*/  
	usCrc = getCrc8( gstrXmodemFrame.pFrameBuf, dataLen );
	if( ( ( UINT8 )( (usCrc >> 8) ) != gstrXmodemFrame.pFrameBuf[ dataLen ]     ) ||
	    ( ( UINT8 )  usCrc        != gstrXmodemFrame.pFrameBuf[ dataLen + 1 ] )  )
	{
		#ifdef XMODEM_DEBUG
		logAdd( "[xmodemGetData]:%d crc is error, No ", dataLen);
		logAdd("%d\n",gstrXmodemFrame.ucframeNo);
		#endif
		iRetryTime++;
		if( iRetryTime >= MAX_RETRY_TIMES )
		{
			#ifdef XMODEM_DEBUG
			logAdd( "[xmodemGetData]:xmodem Get getCrc8 overtime\n", 0);
			#endif
			xmodemTransCancel( );
			return( -1 );
		}
		RS232_BYTE_TX( NAK );
		return( NAK );
	}
	/*接收完一帧数据后，write programe to pflash*/
//	if( func( gstrXmodemFrame.pFrameBuf, dataLen ) == ERROR )
//	{
//		#ifdef XMODEM_DEBUG
//		logAdd( "[xmodemGetData]:write file error\n" ,0);
//		#endif
//		xmodemTransCancel( );
//		return( -1 );
//	}
	#if 0
	else if( gstrXmodemFrame.ucframeType == STX )
	{
		for( i = 0; i < STX_DATA_LENGTH + 2; i++ )
		{
			if( xmodemGetCharInTime( WAIT_DATA_TIME, &pucGetDataBuff[ i ] ) == -1 )
			{
				if( iRetryTime == 10 )
				{
					//logAdd( "[xmodemGetData]:xmodem Get STX data overtime\n\r" );
					return( -1 );
				}
				iRetryTime++;
			}
		}
		
		usCrc = getCrc8( pucGetDataBuff, STX_DATA_LENGTH );
		if( ( ( UINT8 )( usCrc >> 8 ) != pucGetDataBuff[ STX_DATA_LENGTH ]     ) ||
		    ( ( UINT8 )  usCrc        != pucGetDataBuff[ STX_DATA_LENGTH + 1 ] )  )
		{
			//logAdd( "[xmodemGetData]:STX crc is error\n\r" );
			iRetryTime++;
			if( iRetryTime == 10 )
			{
				//logAdd( "[xmodemGetData]:xmodem Get STX data overtime\n\r" );
				return( -1 );
			}
			//xmodemPurge( );
			RS232_BYTE_TX( NAK );			
			return( NAK );
		}

		if( xmodemCopyDataToFile( pucGetDataBuff, STX_DATA_LENGTH ) == ERROR )
		{
			//logAdd( "[xmodemGetData]:write file error\n\r" );
			return( -1 );
		}
	}
	#endif
    iRetryTime = 0;
	return( 0 );
}


/*取消xmodem传输*/
void xmodemTransCancel( void )
{
	RS232_BYTE_TX( CAN );
	RS232_BYTE_TX( CAN );
	RS232_BYTE_TX( CAN );
	RS232_BYTE_TX( CAN );
}	

static void xmodemFreeBuf(void)
{
    if(gstrXmodemFrame.pFrameBuf != NULL)
    {
        free(gstrXmodemFrame.pFrameBuf);
        gstrXmodemFrame.pFrameBuf = NULL;
    }
}



/**************************************************
*xmodem upload main process
*func: write file process
*return: OK or ERROR
*xmodem 下载程序期间，需要关闭中断，同时printf的串口打印禁止使用，打印信息可先记录在buffer中
***************************************************/
INT16 xmodemUpload()
{
	INT16  iIsStartupFlag = 0;
	status_t status = kStatus_Fail;
	
	/**/
	gstrXmodemFrame.ucframeNo = 1;
	gstrXmodemFrame.xmdStat = 0;
	gstrXmodemFrame.pFrameBuf = NULL;

	UINT8 *page_buffers;
	UINT8 *page_buffer;
	UINT8 *first_page;
	UINT32 pagesize;
	UINT32 length;
	UINT32 sector_size;
	UINT32 totalsize = 0;
	UINT16 ret;

	UINT32 partition_start_addr = sencondary_start_address;
	UINT32 write_partition_addr = partition_start_addr;
	UINT32 erase_partition_addr = partition_start_addr;

    pagesize = FLASH_PAGE_SIZE;
	length   = pagesize/2;
	sector_size = FLASH_SECTOR_SIZE;
    page_buffers = (UINT8 *)malloc(pagesize*2);
	first_page = page_buffers;
	page_buffer = page_buffers + pagesize;

	write_partition_addr += pagesize;
	totalsize += pagesize;


	while( 1 )
	{

		ret = (gstrXmodemFrame.ucframeNo+1)%2;

		if( iIsStartupFlag == 0 )
		{
			if( xmodemStartup( ) == ERROR) /*启动xmodem*/
			{
				#ifdef XMODEM_DEBUG
				logAdd( "[xmodemUpload]:xmodemStartup is error\n", 0);
				#endif
				xmodemFreeBuf();
				return ERROR;
			}
			status = mem_erase(erase_partition_addr,sector_size,0);
			if( status != kStatus_Success)
			{
				break;
			}
			erase_partition_addr += sector_size;
			iIsStartupFlag = 1;
		}
		else
		{
			switch( xmodemGetHead() ) /*获取帧头*/
			{
				case 0:
					break; /*接收帧头成功，且非结束帧*/	
				case EOT:
				{
					/*成功接收到所有数据包并写入flash成功*/
					#ifdef XMODEM_DEBUG
					if ((iIsStartupFlag = logAdd("xmodem Upload complete.\n",0))>0)
						printf ("logAdd error %d\n", iIsStartupFlag);
					#endif
                                 	xmodemFreeBuf();
					//printf ("\nxmodem Upload complete.\n",0);
                    if(!ret)
					{
						memset(page_buffer+length, 0xff, length);
						mem_write(write_partition_addr, pagesize,  &page_buffer[0], 0);
						totalsize += pagesize;
					}

					boot_image_header_t *bih = (boot_image_header_t *)first_page;
					if ((bih->image_size == 0) && (bih->header_size >= pagesize) && (bih->header_size < totalsize))
					{
						
						bih->image_size  = totalsize - bih->header_size;
						bih->checksum[0] = bl_crc32((uint8_t *)partition_start_addr + bih->header_size, bih->image_size);
						bih->algorithm   = IMG_CHK_ALG_CRC32;						
					}
					else 
					{
						return ERROR;
					}

					status = mem_write(partition_start_addr, pagesize,  &first_page[0], 0);
					if( status != kStatus_Success)
					{
						break;
					}

                    
					debug_printf("upload complete.image size:%x\n\r", bih->image_size);
					free(page_buffers);


					return OK;
				}
				case CAN:
				{
					#ifdef XMODEM_DEBUG
					logAdd( "[xmodemUpload]:xmodemGetHead CAN\n", 0);
					#endif
                                 	xmodemFreeBuf();
					return ERROR;	
				}
				case -1:
				{
					#ifdef XMODEM_DEBUG
					logAdd( "[xmodemUpload]:xmodemGetHead -1\n", 0);
					#endif
                                 	xmodemFreeBuf();
					return ERROR;
				}
                           default:
                                xmodemFreeBuf();
                                return ERROR;
			}
    		}
    	
		switch( xmodemGetFrameId( ) )  /*获取帧序列id及补码*/
		{
			case 0:
				break; /*接收帧序列号正确*/
			case NAK:
				continue; /*接收帧序列号错误*/
			case -1: 
			{
				#ifdef XMODEM_DEBUG
				logAdd( "[xmodemUpload]:xmodemGetFrameId -1\n", 0);
				#endif
                           	xmodemFreeBuf();
				return ERROR;
			}
                    default:
                           xmodemFreeBuf();
                           return ERROR;
		}
    	
		switch( xmodemGetData() ) /*逐个帧接收，并写入pflash指定区域*/
		{
			case 0:
			{
				if((gstrXmodemFrame.ucframeNo == 1)||(gstrXmodemFrame.ucframeNo == 2))
				{
					memcpy(&first_page[ret*length], (CHAR8 *)&gstrXmodemFrame.pFrameBuf[0], length);
					
					if(gstrXmodemFrame.ucframeNo == 2)
					{
						boot_image_header_t *image_header = (boot_image_header_t *)first_page;
						
						if(image_header->tag != IMG_HDR_TAG)
						{
						    return ERROR;
						}
					}
					
				}
				else
				{
					memcpy(&page_buffer[ret*length], (CHAR8 *)&gstrXmodemFrame.pFrameBuf[0], length);
				}
				if(ret&&(gstrXmodemFrame.ucframeNo != 2))
				{
					if(erase_partition_addr <= write_partition_addr)
					{
						status = mem_erase(erase_partition_addr,sector_size,0);
						if( status != kStatus_Success)
						{
							break;
						}
			        	erase_partition_addr += sector_size;
					}

						status = mem_write(write_partition_addr, pagesize, &page_buffer[0], 0);
						if( status != kStatus_Success)
						{
							break;
						}
					    write_partition_addr += pagesize;
						totalsize += pagesize;
				}

				/*成功接收到数据后帧号加1*/
				gstrXmodemFrame.ucframeNo++;   
				//if(0x100 == gstrXmodemFrame.ucframeNo)
					//gstrXmodemFrame.ucframeNo = 0;   /*wraps 0FFH to 00H*/
				RS232_BYTE_TX( ACK );
				#ifdef XMODEM_DEBUG
				//logAdd( "[xmodemUpload]:xmodemGetData ok\n", 0);
				#endif
				break;
			}
			case NAK:
				#ifdef XMODEM_DEBUG
				logAdd( "[xmodemUpload]:xmodemGetData NAK\n", 0);
				#endif
				continue;    	    	
			case -1:
			{
				#ifdef XMODEM_DEBUG
				logAdd( "[xmodemUpload]:xmodemGetData is error\n", 0);
				#endif
                           xmodemFreeBuf();
				return ERROR;
			}
                    default:
                           xmodemFreeBuf();
                           return ERROR;
		}
	}
	//return OK;
}

#ifdef ENABLE_XDOWNLOAD
INT16 flashread (UINT8 * dest, UINT8 ** src, INT16 num)
{
/*	INT16 i;
	static INT16 readnum = 0;
	UINT8 * s;
	s = *src;
	
	for (i=0;i<num;i++)
	{
		//if ((*s == 0)&&(*(s+1) == 0))
		//	break;
		if (readnum>1023)
		  break;
		*dest++=*s++;
	}
	*src = s;
	readnum+=i;*/
	
	/*static UINT16 rPage = 0;
	static UINT16 rAddr = 0;
	 
	spi_readflash (rPage, rAddr, dest, (UINT32)num);
	if (rAddr + num > 264) 
	{
	  rAddr = rAddr + num - 264;
	  rPage++;
	} else
	 rAddr+=  num;
	 if (rPage > 600)
	return OK;
	else
	return num;*/
	 static UINT32 faddr = 0;
	//spi_randomRead (faddr, dest, (UINT32)num);
	if (faddr > 160000)
		return OK;
	else
		faddr += num;
	return num;
}

/*xmodem下载文件到PC机启动函数 */
INT16 xmodemDownload(void)
{
	//INT16  fd;
	CHAR8 *data_file;
	CHAR8 packet_data[ XMODEM_DATA_SIZE ];  /*数据包*/
	CHAR8 frame_data[ XMODEM_DATA_SIZE + XMODEM_CRC_SIZE + XMODEM_FRAME_ID_SIZE + 1 ];  /*1 帧数据*/
	//UINT8 tmp;
	//FILE  *datafile;
	INT16  complete,retry_num,pack_counter,read_number,write_number,i;
	UINT16 crc_value;
	UINT8  ack_id;
	INT16    err = 0; 
	
	printf("\nxmodem Download started...\n\r",0);

	/*data_file_name = fileName;
	if((datafile=fopen(data_file_name,"rb"))==NULL)
	{
	 logAdd ("Can't open file!\n\r",);
	 return -1 ;
	}
    if( ( fd = open( "/tyCo/0", O_RDWR , 0 ) ) == ERROR )
    {
        logAdd("[ xmodemDownload ] : open Com failure\n\r" );
		fclose(datafile);
        return( -1 );
    }
	close(gStdFd);
	if( ( gStdFd = open ( "/tffs0/xmodemConsole.dat", O_RDWR | O_CREAT , 0644 ) ) == ERROR )
    {
    	logAdd(" [ xmodemDownload ]: creat file failed\n\r");
    	return( -1 );
    }*/
	
	/*重定位STDIO，因为在xmodem要利用这个串口传输文件*/
	/*gOldStdFd = ioGlobalStdGet(STD_IN);		
	ioGlobalStdSet( STD_IN,  gStdFd);
	ioGlobalStdSet( STD_OUT, gStdFd);
	ioGlobalStdSet( STD_ERR, gStdFd);

	ioctl( fd, FIOBAUDRATE, 115200 );
	ioctl (fd, FIOSETOPTIONS, OPT_RAW);  */ /*必须设置为原始模式*/
	//ioctl (fd, FIOFLUSH, 0);
	
	/*******************************/
	pack_counter = 0;
	complete = 0;
	retry_num = 0;
	data_file = &xmodemDataBuf[0];
	SCI0_rxbuf = 0;
	do
	{
		micSecDelay(50);
	}while(SCI0_rxbuf != OUTC);
	
	ack_id = ACK;
	
	while(!complete)
	{
	 switch(ack_id)
	 {
	 case ACK:
	  retry_num = 0;
	  pack_counter++;
	  
	  //(void) ioctl (fd, FIORFLUSH,  0); /* 丢弃接收缓冲区中的数据 */
	  
	  read_number = flashread( packet_data, &data_file, XMODEM_DATA_SIZE);
	  if(read_number>0)
	  {
	   if(read_number<XMODEM_DATA_SIZE_SOH)
	   {
	  
		//logAdd("Start filling the last frame!\r\n");
		for(;read_number<XMODEM_DATA_SIZE;read_number++)
		 packet_data[read_number] = 0x0;
	   }
	   frame_data[0] = XMODEM_HEAD;
	   frame_data[1] = (CHAR8)pack_counter;
	   frame_data[2] = (CHAR8)(255-frame_data[1]);
	
	   for(i=0;i<XMODEM_DATA_SIZE;i++)
           frame_data[i+3]=packet_data[i];
	
	   crc_value = getCrc8(packet_data,XMODEM_DATA_SIZE);
	   frame_data[XMODEM_DATA_SIZE+XMODEM_FRAME_ID_SIZE+1]=(UINT8)(crc_value >> 8);
	   frame_data[XMODEM_DATA_SIZE+XMODEM_FRAME_ID_SIZE+2]=(UINT8)(crc_value);
	   
		RS232_STRING_TX(frame_data, XMODEM_DATA_SIZE_SOH + 5);

	   //logAdd("waiting for ACK,%d,%d,...",pack_counter,write_number);
	
	   ack_id = 0x0;
	   SCI0_PollStrRx (&ack_id, 1, 0);//SCI0_PollStrRx(&ack_id,1);  /*读ACK回复信息*/
	   /*if(ack_id == ACK)
	      logAdd("Ok!\r\n");
	   else
		  logAdd("Error!\r\n");*/
	  }
	  else
	  {
	   ack_id = EOT;
	   //(void) ioctl (fd, FIORFLUSH,  0);
	   
	   RS232_BYTE_TX(ack_id); /*写结束标志*/

	   //logAdd("Waiting for complete ACK ...");
	   ack_id = 0x0;
	   SCI0_PollStrRx(&ack_id, 1, 0);//SCI0_PollStrRx(&ack_id,1);  /*读ACK回复信息*/
	   /*if(ack_id == ACK)
	      logAdd("Ok!\r\n");
	   else
		  logAdd("Error!\r\n");
	   
	   logAdd("Sending file complete\r\n"); */
	   complete = 1;
	  }
	 break;
	
	 case NAK:
	  if( retry_num++ > 10) 
	  {
	   //logAdd("Retry too many times,Quit!\r\n");
	   complete = 1;
	  }
	  else
	  {
	  //(void) ioctl (fd, FIORFLUSH,  0);
	  
	  RS232_STRING_TX(frame_data, XMODEM_DATA_SIZE + 5);
		 write_number = XMODEM_DATA_SIZE + 5;
	   //logAdd("Retry for ACK,%d,%d...",pack_counter,write_number);

	   ack_id = 0x0;
	   SCI0_PollStrRx(&ack_id, 1, 0);//SCI0_PollStrRx(&ack_id,1);     /*读ACK回复信息*/
	   /*if( ack_id == ACK )
		logAdd("OK\r\n");
	   else
		logAdd("Error!\r\n");*/
	  }
	 break;
	
	 default:
	  //logAdd("Fatal Error!\r\n");
	  err = 1;
	  complete = 1;
	 break;
	 }
	
	}

  /* (void) ioctl (gOldStdFd, FIOSETOPTIONS, OPT_TERMINAL);
   ioGlobalStdSet( STD_IN,  gOldStdFd);
   ioGlobalStdSet( STD_OUT, gOldStdFd);
   ioGlobalStdSet( STD_ERR, gOldStdFd);
   close(gStdFd);

   fclose(datafile);
   close(fd);*/

   if(err)
   	{
   	  printf("\nsend file canceled\n\r",0);
	  return -1;
   	}
   else
   	{
      printf("\nSending file complete.\n\r",0);
	  return OK;
   	}
   
}
#endif

/*发起XMODEM上载文件的任务*/
/* 有问题
STATUS xmodemRecv(CHAR8 *fileName)
{  
   STATUS stat; 
   
   stat = taskSpawn("tXmodem", 180, 0, 8192,    
			(FUNCPTR)xmodemDownLoad, fileName,0,0,0,0,0,0,0,0,0);	
   return(stat);
}
*/

