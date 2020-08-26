#include "enc28j60.h"
#include "stdio.h"
#include "stm32f103xb.h"
#include "stm32f1xx_ll_spi.h"
#include "stm32f1xx_ll_gpio.h"

static uint8_t Enc28j60Bank;
static uint32_t NextPacketPtr;

#define ENC28J60_ENABLE()       LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_4)
#define ENC28J60_DISABLE()      LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_4)

static uint8_t spi_ll_wr(uint8_t dat)
{
  uint8_t retry=0;
  while(!LL_SPI_IsActiveFlag_TXE(SPI1)){
    retry++;
    if (retry > 200)
      return 0;
  }
  LL_SPI_TransmitData8(SPI1, dat);
  
  retry = 0;
  while(!LL_SPI_IsActiveFlag_RXNE(SPI1)){
    retry++;
    if (retry > 200)
      return 0;
  }
  return LL_SPI_ReceiveData8(SPI1);
}

uint8_t enc28j60ReadOp(uint8_t op, uint8_t address)
{
  uint8_t dat = 0;

  ENC28J60_ENABLE();
  
  dat = op | (address & ADDR_MASK);
  spi_ll_wr(dat);
  dat = spi_ll_wr(0xFF);
  // do dummy read if needed (for mac and mii, see datasheet page 29)
  if(address & 0x80)
  {
    dat = spi_ll_wr(0xFF);
  }
  // release CS
  ENC28J60_DISABLE();
  return dat;
}

void enc28j60WriteOp(uint8_t op, uint8_t address, uint8_t data)
{
  uint8_t dat = 0;
  
  ENC28J60_ENABLE();
  // issue write command
  dat = op | (address & ADDR_MASK);
  spi_ll_wr(dat);
  // write data
  //  dat = data;
  spi_ll_wr(data);
  ENC28J60_DISABLE();
}

void enc28j60ReadBuffer(uint32_t len, uint8_t* data)
{
  ENC28J60_ENABLE();
  // issue read command
  spi_ll_wr(ENC28J60_READ_BUF_MEM);
  while(len)
  {
    len--;
    // read data
    *data = (uint8_t)spi_ll_wr(0);
    data++;
  }
  *data='\0';
  ENC28J60_DISABLE();
}

void enc28j60WriteBuffer(uint32_t len, uint8_t* data)
{
  ENC28J60_ENABLE();
  // issue write command
  spi_ll_wr(ENC28J60_WRITE_BUF_MEM);
  
  while(len)
  {
    len--;
    spi_ll_wr(*data);
    data++;
  }
  ENC28J60_DISABLE();
}

void enc28j60SetBank(uint8_t address)
{
  // set the bank (if needed)
  if((address & BANK_MASK) != Enc28j60Bank)
  {
    // set the bank
    enc28j60WriteOp(ENC28J60_BIT_FIELD_CLR, ECON1, (ECON1_BSEL1|ECON1_BSEL0));
    enc28j60WriteOp(ENC28J60_BIT_FIELD_SET, ECON1, (address & BANK_MASK)>>5);
    Enc28j60Bank = (address & BANK_MASK);
  }
}

uint8_t enc28j60Read(uint8_t address)
{
  // set the bank
  enc28j60SetBank(address);
  // do the read
  return enc28j60ReadOp(ENC28J60_READ_CTRL_REG, address);
}

void enc28j60Write(uint8_t address, uint8_t data)
{
  // set the bank
  enc28j60SetBank(address);
  // do the write
  enc28j60WriteOp(ENC28J60_WRITE_CTRL_REG, address, data);
}

void enc28j60PhyWrite(uint8_t address, uint32_t data)
{
  // set the PHY register address
  enc28j60Write(MIREGADR, address);
  // write the PHY data
  enc28j60Write(MIWRL, data);
  enc28j60Write(MIWRH, data>>8);
  // wait until the PHY write completes
  
  while(enc28j60Read(MISTAT) & MISTAT_BUSY)
  {
    //Del_10us(1);
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
  }
}

void enc28j60clkout(uint8_t clk)
{
  //setup clkout: 2 is 12.5MHz:
  enc28j60Write(ECOCON, clk & 0x7);
}

uint8_t enc28j60Init(uint8_t* macaddr)
{  
  uint16_t retry = 0;
  uint8_t address[6];
  
  ENC28J60_DISABLE();	      
  enc28j60WriteOp(ENC28J60_SOFT_RESET, 0, ENC28J60_SOFT_RESET);
  while(!(enc28j60Read(ESTAT)&ESTAT_CLKRDY)&&retry<500)
  {
    retry++;
    //		delay_ms(1);
  };
  if(retry>=500)
    return 1;//ENC28J60��ʼ��ʧ��
  //	Del_1ms(250);
  // check CLKRDY bit to see if reset is complete
  // The CLKRDY does not work. See Rev. B4 Silicon Errata point. Just wait.
  //while(!(enc28j60Read(ESTAT) & ESTAT_CLKRDY));
  // do bank 0 stuff
  // initialize receive buffer
  // 16-bit transfers, must write low byte first
  // set receive buffer start address	   ���ý��ջ�������ַ  8K�ֽ�����
  NextPacketPtr = RXSTART_INIT;
  // Rx start
  //���ջ�������һ��Ӳ��������ѭ��FIFO ���������ɡ�
  //�Ĵ�����ERXSTH:ERXSTL ��ERXNDH:ERXNDL ��
  //Ϊָ�룬���建���������������ڴ洢���е�λ�á�
  //ERXST��ERXNDָ����ֽھ�������FIFO�������ڡ�
  //������̫���ӿڽ��������ֽ�ʱ����Щ�ֽڱ�˳��д��
  //���ջ������� ���ǵ�д����ERXND ָ��Ĵ洢��Ԫ
  //��Ӳ�����Զ������յ���һ�ֽ�д����ERXST ָ��
  //�Ĵ洢��Ԫ�� ��˽���Ӳ��������д��FIFO ����ĵ�
  //Ԫ��
  enc28j60Write(ERXSTL, RXSTART_INIT&0xFF);	 //
  enc28j60Write(ERXSTH, RXSTART_INIT>>8);
  // set receive pointer address
  //ERXWRPTH:ERXWRPTL �Ĵ�������Ӳ����FIFO ��
  //���ĸ�λ��д������յ����ֽڡ� ָ����ֻ���ģ��ڳ�
  //�����յ�һ�����ݰ���Ӳ�����Զ�����ָ�롣 ָ���
  //�����ж�FIFO ��ʣ��ռ�Ĵ�С  8K-1500�� 
  enc28j60Write(ERXRDPTL, RXSTART_INIT&0xFF);
  enc28j60Write(ERXRDPTH, RXSTART_INIT>>8);
  // RX end
  enc28j60Write(ERXNDL, RXSTOP_INIT&0xFF);
  enc28j60Write(ERXNDH, RXSTOP_INIT>>8);
  // TX start	  1500
  enc28j60Write(ETXSTL, TXSTART_INIT&0xFF);
  enc28j60Write(ETXSTH, TXSTART_INIT>>8);
  // TX end
  enc28j60Write(ETXNDL, TXSTOP_INIT&0xFF);
  enc28j60Write(ETXNDH, TXSTOP_INIT>>8);
  // do bank 1 stuff, packet filter:
  // For broadcast packets we allow only ARP packtets
  // All other packets should be unicast only for our mac (MAADR)
  //
  // The pattern to match on is therefore
  // Type     ETH.DST
  // ARP      BROADCAST
  // 06 08 -- ff ff ff ff ff ff -> ip checksum for theses bytes=f7f9
  // in binary these poitions are:11 0000 0011 1111
  // This is hex 303F->EPMM0=0x3f,EPMM1=0x30
  //���չ�����
  //UCEN������������ʹ��λ
  //��ANDOR = 1 ʱ��
  //1 = Ŀ���ַ�뱾��MAC ��ַ��ƥ������ݰ���������
  //0 = ��ֹ������
  //��ANDOR = 0 ʱ��
  //1 = Ŀ���ַ�뱾��MAC ��ַƥ������ݰ��ᱻ����
  //0 = ��ֹ������
  
  //CRCEN���������CRC У��ʹ��λ
  //1 = ����CRC ��Ч�����ݰ�����������
  //0 = ������CRC �Ƿ���Ч
  
  //PMEN����ʽƥ�������ʹ��λ
  //��ANDOR = 1 ʱ��
  //1 = ���ݰ�������ϸ�ʽƥ�����������򽫱�����
  //0 = ��ֹ������
  //��ANDOR = 0 ʱ��
  //1 = ���ϸ�ʽƥ�����������ݰ���������
  //0 = ��ֹ������
  enc28j60Write(ERXFCON, ERXFCON_UCEN | ERXFCON_CRCEN | ERXFCON_PMEN);
  //  enc28j60Write(ERXFCON, ERXFCON_UCEN | ERXFCON_PMEN);
  enc28j60Write(EPMM0, 0x3f);
  enc28j60Write(EPMM1, 0x30);
  enc28j60Write(EPMCSL, 0xf9);
  enc28j60Write(EPMCSH, 0xf7);
  // do bank 2 stuff
  // enable MAC receive
  //bit 0 MARXEN��MAC ����ʹ��λ
  //1 = ����MAC �������ݰ�
  //0 = ��ֹ���ݰ�����
  //bit 3 TXPAUS����ͣ����֡����ʹ��λ
  //1 = ����MAC ������ͣ����֡������ȫ˫��ģʽ�µ��������ƣ�
  //0 = ��ֹ��ͣ֡����
  //bit 2 RXPAUS����ͣ����֡����ʹ��λ
  //1 = �����յ���ͣ����֡ʱ����ֹ���ͣ�����������
  //0 = ���Խ��յ�����ͣ����֡
  enc28j60Write(MACON1, MACON1_MARXEN | MACON1_TXPAUS | MACON1_RXPAUS);
  // bring MAC out of reset
  //��MACON2 �е�MARST λ���㣬ʹMAC �˳���λ״̬��
  enc28j60Write(MACON2, 0x00);
  // enable automatic padding to 60bytes and CRC operations
  //bit 7-5 PADCFG2:PACDFG0���Զ�����CRC ����λ
  //111 = ��0 ������ж�֡��64 �ֽڳ�����׷��һ����Ч��CRC
  //110 = ���Զ�����֡
  //101 = MAC �Զ�������8100h �����ֶε�VLAN Э��֡�����Զ���䵽64 �ֽڳ��������
  //��VLAN ֡���������60 �ֽڳ�������Ҫ׷��һ����Ч��CRC
  //100 = ���Զ�����֡
  //011 = ��0 ������ж�֡��64 �ֽڳ�����׷��һ����Ч��CRC
  //010 = ���Զ�����֡
  //001 = ��0 ������ж�֡��60 �ֽڳ�����׷��һ����Ч��CRC
  //000 = ���Զ�����֡
  //bit 4 TXCRCEN������CRC ʹ��λ
  //1 = ����PADCFG��Σ�MAC�����ڷ���֡��ĩβ׷��һ����Ч��CRC�� ���PADCFG�涨Ҫ
  //׷����Ч��CRC������뽫TXCRCEN ��1��
  //0 = MAC����׷��CRC�� ������4 ���ֽڣ����������Ч��CRC �򱨸������״̬������
  //bit 0 FULDPX��MAC ȫ˫��ʹ��λ
  //1 = MAC������ȫ˫��ģʽ�¡� PHCON1.PDPXMD λ������1��
  //0 = MAC�����ڰ�˫��ģʽ�¡� PHCON1.PDPXMD λ�������㡣
  enc28j60WriteOp(ENC28J60_BIT_FIELD_SET, MACON3, MACON3_PADCFG0 | MACON3_TXCRCEN | MACON3_FRMLNEN | MACON3_FULDPX);
  // set inter-frame gap (non-back-to-back)
  //���÷Ǳ��Ա��������Ĵ����ĵ��ֽ�
  //MAIPGL�� �����Ӧ��ʹ��12h ��̸üĴ�����
  //���ʹ�ð�˫��ģʽ��Ӧ��̷Ǳ��Ա�������
  //�Ĵ����ĸ��ֽ�MAIPGH�� �����Ӧ��ʹ��0Ch
  //��̸üĴ�����
  enc28j60Write(MAIPGL, 0x12);
  enc28j60Write(MAIPGH, 0x0C);
  // set inter-frame gap (back-to-back)
  //���ñ��Ա��������Ĵ���MABBIPG����ʹ��
  //ȫ˫��ģʽʱ�������Ӧ��ʹ��15h ��̸üĴ�
  //������ʹ�ð�˫��ģʽʱ��ʹ��12h ���б�̡�
  enc28j60Write(MABBIPG, 0x15);
  // Set the maximum packet size which the controller will accept
  // Do not send packets longer than MAX_FRAMELEN:
  // ���֡����  1500
  enc28j60Write(MAMXFLL, MAX_FRAMELEN&0xFF);	
  enc28j60Write(MAMXFLH, MAX_FRAMELEN>>8);
  // do bank 3 stuff
  // write MAC address
  // NOTE: MAC address in ENC28J60 is byte-backward
  enc28j60Write(MAADR5, macaddr[0]);	
  enc28j60Write(MAADR4, macaddr[1]);
  enc28j60Write(MAADR3, macaddr[2]);
  enc28j60Write(MAADR2, macaddr[3]);
  enc28j60Write(MAADR1, macaddr[4]);
  enc28j60Write(MAADR0, macaddr[5]);
  //����PHYΪȫ˫��  LEDBΪ������
  enc28j60PhyWrite(PHCON1, PHCON1_PDPXMD);
  // no loopback of transmitted frames	 ��ֹ����
  //HDLDIS��PHY ��˫�����ؽ�ֹλ
  //��PHCON1.PDPXMD = 1 ��PHCON1.PLOOPBK = 1 ʱ��
  //��λ�ɱ����ԡ�
  //��PHCON1.PDPXMD = 0 ��PHCON1.PLOOPBK = 0 ʱ��
  //1 = Ҫ���͵����ݽ�ͨ��˫���߽ӿڷ���
  //0 = Ҫ���͵����ݻỷ�ص�MAC ��ͨ��˫���߽ӿڷ���
  enc28j60PhyWrite(PHCON2, PHCON2_HDLDIS);
  // switch to bank 0
  //ECON1 �Ĵ���
  //�Ĵ���3-1 ��ʾΪECON1 �Ĵ����������ڿ���
  //ENC28J60 ����Ҫ���ܡ� ECON1 �а�������ʹ�ܡ���
  //������DMA ���ƺʹ洢��ѡ��λ��
  
  enc28j60SetBank(ECON1);
  // enable interrutps
  //EIE�� ��̫���ж������Ĵ���
  //bit 7 INTIE�� ȫ��INT �ж�����λ
  //1 = �����ж��¼�����INT ����
  //0 = ��ֹ����INT ���ŵĻ������ʼ�ձ�����Ϊ�ߵ�ƽ��
  //bit 6 PKTIE�� �������ݰ��������ж�����λ
  //1 = �����������ݰ��������ж�
  //0 = ��ֹ�������ݰ��������ж�
  enc28j60WriteOp(ENC28J60_BIT_FIELD_SET, EIE, EIE_INTIE|EIE_PKTIE);
  // enable packet reception
  //bit 2 RXEN������ʹ��λ
  //1 = ͨ����ǰ�����������ݰ�����д����ջ�����
  //0 = �������н��յ����ݰ�
  enc28j60WriteOp(ENC28J60_BIT_FIELD_SET, ECON1, ECON1_RXEN);
  
  address[0] = enc28j60Read(MAADR5);
  address[1] = enc28j60Read(MAADR4);
  address[2] = enc28j60Read(MAADR3);
  address[3] = enc28j60Read(MAADR2);
  address[4] = enc28j60Read(MAADR1);
  address[5] = enc28j60Read(MAADR0);
//  printf("%02x %02x %02x %02x %02x %02x\r\n",
//         address[0], address[1], address[2], address[3], address[4], address[5]);
//  
  for (retry = 0; retry < 6; retry++) {
    if ( address[retry] != macaddr[5-retry])
      return 0;
  }
  
  return 1;
}

// read the revision of the chip:
uint8_t enc28j60getrev(void)
{
  //��EREVID ��Ҳ�洢�˰汾��Ϣ�� EREVID ��һ��ֻ����
  //�ƼĴ���������һ��5 λ��ʶ����������ʶ�����ض���Ƭ
  //�İ汾��
  return(enc28j60Read(EREVID));
}

void enc28j60PacketSend(uint32_t len, uint8_t* packet)
{
  // Set the write pointer to start of transmit buffer area
  enc28j60Write(EWRPTL, TXSTART_INIT&0xFF);
  enc28j60Write(EWRPTH, TXSTART_INIT>>8);
  
  // Set the TXND pointer to correspond to the packet size given
  enc28j60Write(ETXNDL, (TXSTART_INIT+len)&0xFF);
  enc28j60Write(ETXNDH, (TXSTART_INIT+len)>>8);
  
  // write per-packet control byte (0x00 means use macon3 settings)
  enc28j60WriteOp(ENC28J60_WRITE_BUF_MEM, 0, 0x00);
  
  // copy the packet into the transmit buffer
  enc28j60WriteBuffer(len, packet);
  
  // send the contents of the transmit buffer onto the network
  enc28j60WriteOp(ENC28J60_BIT_FIELD_SET, ECON1, ECON1_TXRTS);
  
  // Reset the transmit logic problem. See Rev. B4 Silicon Errata point 12.
  if( (enc28j60Read(EIR) & EIR_TXERIF) )
  {
    enc28j60WriteOp(ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_TXRTS);
  }
}

// Gets a packet from the network receive buffer, if one is available.
// The packet will by headed by an ethernet header.
//      maxlen  The maximum acceptable length of a retrieved packet.
//      packet  Pointer where packet data should be stored.
// Returns: Packet length in bytes if a packet was retrieved, zero otherwise.
uint32_t enc28j60PacketReceive(uint32_t maxlen, uint8_t* packet)
{
  uint32_t rxstat;
  uint32_t len;
  
  // check if a packet has been received and buffered
  //if( !(enc28j60Read(EIR) & EIR_PKTIF) ){
  // The above does not work. See Rev. B4 Silicon Errata point 6.
  if( enc28j60Read(EPKTCNT) ==0 )  //�յ�����̫�����ݰ�����
  {
    return(0);
  }
  
  // Set the read pointer to the start of the received packet		 ��������ָ��
  enc28j60Write(ERDPTL, (NextPacketPtr));
  enc28j60Write(ERDPTH, (NextPacketPtr)>>8);
  
  // read the next packet pointer
  NextPacketPtr  = enc28j60ReadOp(ENC28J60_READ_BUF_MEM, 0);
  NextPacketPtr |= enc28j60ReadOp(ENC28J60_READ_BUF_MEM, 0)<<8;
  
  // read the packet length (see datasheet page 43)��ȡ���ݰ�����
  len  = enc28j60ReadOp(ENC28J60_READ_BUF_MEM, 0);
  len |= enc28j60ReadOp(ENC28J60_READ_BUF_MEM, 0)<<8;
  
  len-=4; //remove the CRC count
  // read the receive status (see datasheet page 43)������״̬
  rxstat  = enc28j60ReadOp(ENC28J60_READ_BUF_MEM, 0);
  rxstat |= enc28j60ReadOp(ENC28J60_READ_BUF_MEM, 0)<<8;
  // limit retrieve length
  if (len>maxlen-1)
  {
    len=maxlen-1;
  }
  
  // check CRC and symbol errors (see datasheet page 44, table 7-3):
  // The ERXFCON.CRCEN is set by default. Normally we should not
  // need to check this.
  if ((rxstat & 0x80)==0)
  {
    // invalid
    len=0;
  }
  else
  {
    // copy the packet from the receive buffer�������ݰ����ջ�����
    enc28j60ReadBuffer(len, packet);
  }
  // Move the RX read pointer to the start of the next received packet
  // This frees the memory we just read out
  enc28j60Write(ERXRDPTL, (NextPacketPtr));
  enc28j60Write(ERXRDPTH, (NextPacketPtr)>>8);
  
  // decrement the packet counter indicate we are done with this packet
  enc28j60WriteOp(ENC28J60_BIT_FIELD_SET, ECON2, ECON2_PKTDEC);
  return(len);
}