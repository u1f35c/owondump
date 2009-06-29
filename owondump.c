/*
 * owondump.c   A userspace USB driver that interrogates an Owon PDS digital oscilloscope
 * 				and gets it to dump its trace memory for all channels, in both vectorgram
 * 				format, and/or in .BMP bitmap format.
 *
 * 				The vectorgram is also parsed into a tabulated text format that can be
 * 				plotted using GNUplot or a similar package.
 *
 * 				Copyright June 2009, Michael Murphy <ee07m060@elec.qmul.ac.uk>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <usb.h>
#include <string.h>
#include <asm/errno.h>
#include <arpa/inet.h> // for htonl() macro
#include "owondump.h"

int verbose = 0;
int debug = 0;

struct usb_device *usb_locks[MAX_USB_LOCKS];
int locksFound = 0;

char *filename = "output.bin";			  // default output filename
int text = 1;							  // tabulated text output as well as raw data output
int channelcount = 0;					  // the number of channels in the data dump

struct channelHeader headers[10];		  // provide for up to ten scpope channels

int decodeVertSensCode(long int i) {
// This is the one byte vertical sensitivity code
	int vertSensitivity=-1;
	switch (i) {
	  case 0x01	: vertSensitivity = 5;		// 5mV
				  break;
	  case 0x02 : vertSensitivity = 10;
				  break;
	  case 0x03 : vertSensitivity = 20;
				  break;
	  case 0x04 : vertSensitivity = 50;
	  			  break;
	  case 0x05	: vertSensitivity = 100;
				  break;
	  case 0x06 : vertSensitivity = 200;
				  break;
	  case 0x07 : vertSensitivity = 500;
				  break;
	  case 0x08 : vertSensitivity = 1000;
	  			  break;
	  case 0x09 : vertSensitivity = 2000;
				  break;
	  case 0x0A : vertSensitivity = 5000;	// 5V
				  break;
	}
	return vertSensitivity;
}


long long int decodeTimebase(long int i) {
	long long int timebase=-1;
// c is the one byte timebase code
// return an LL int

	switch (i) {
		  case 0x00	: timebase = 5LL;	// in nanoseconds
					  break;
		  case 0x01	: timebase = 10LL;
					  break;
		  case 0x02 : timebase = 25LL;
					  break;
		  case 0x03	: timebase = 50LL;
					  break;
		  case 0x04	: timebase = 100LL;
					  break;
		  case 0x05	: timebase = 250LL;
					  break;
		  case 0x06	: timebase = 500LL;
					  break;
		  case 0x07	: timebase = 1000LL; // 1 microsecond
					  break;
		  case 0x08	: timebase = 2500LL;
					  break;
		  case 0x09	: timebase = 5000LL;
					  break;
		  case 0x0a : timebase = 10000LL;
					  break;
		  case 0x0b	: timebase = 25000LL;
					  break;
		  case 0x0c	: timebase = 50000LL;
					  break;
		  case 0x0d	: timebase = 100000LL;
					  break;
		  case 0x0e	: timebase = 250000LL;
					  break;
		  case 0x0f	: timebase = 500000LL;
					  break;
		  case 0x10	: timebase = 1000000LL;	// 1 millisecond
					  break;
		  case 0x11 : timebase = 2500000LL;
					  break;
		  case 0x12	: timebase = 5000000LL;
					  break;
		  case 0x13	: timebase = 10000000LL;	// 10 ms
					  break;
		  case 0x14	: timebase = 25000000LL;
					  break;
		  case 0x15	: timebase = 50000000LL;
					  break;
		  case 0x16	: timebase = 100000000LL;	// 100 ms
					  break;
		  case 0x17	: timebase = 250000000LL;
					  break;
		  case 0x18	: timebase = 500000000LL;
					  break;
		  case 0x19	: timebase = 1000000000LL;	// 1 second
					  break;
		  case 0x1a	: timebase = 2500000000LL;
					  break;
		  case 0x1b	: timebase = 5000000000LL;
					  break;
		  case 0x1c	: timebase = 10000000000LL;
					  break;
		  case 0x1d	: timebase = 25000000000LL;
					  break;
		  case 0x1e	: timebase = 50000000000LL;
					  break;
		  case 0x1f	: timebase = 100000000000LL;	// 100 seconds
					  break;
	}
	return timebase;
}

// decode the contents of the vectorgram data header - providing us with the timebase and voltage values for the channel data
// hdrBuf has already been stripped of the 10 byte vectorgram file header that begins "SPBV......"
// returns the size of the channel data block in bytes

struct channelHeader decodeVectorgramBufferHeader(char *hdrBuf) {
	struct channelHeader header;

// if not a bitmap then must be a vectorgram
	memcpy(&header.channelname,hdrBuf,3);
	header.channelname[3]='\0';
	memcpy(&header.blocklength, hdrBuf+3,4);
	memcpy(&header.unknown1, hdrBuf+7,4);
	memcpy(&header.unknown2, hdrBuf+11,4);
	memcpy(&header.unknown3, hdrBuf+15,4);
	memcpy(&header.timebasecode, hdrBuf+19,4);
	memcpy(&header.unknown4, hdrBuf+23,4);
	memcpy(&header.vertsenscode, hdrBuf+27,4);
	memcpy(&header.unknown5, hdrBuf+31,4);
	memcpy(&header.unknown6, hdrBuf+35,4);
	memcpy(&header.unknown7, hdrBuf+39,4);
	memcpy(&header.unknown8, hdrBuf+43,4);
	memcpy(&header.unknown9, hdrBuf+43,4);


	header.vertSensitivity = decodeVertSensCode(header.vertsenscode);	// 5mV through 5000mV (5V)
	header.timeBase = decodeTimebase(header.timebasecode);		    	// in nanoseconds (10E-9)

	printf("-------------------------------\n");
	printf("              Channel: %s\n", header.channelname);
	printf("         sample count: %d\n", (int) header.blocklength / 2);
	printf(" vertical sensitivity: %dmV\n", header.vertSensitivity);
	printf("             timebase: %gms\n", (double) header.timeBase / 1000000);
	printf("-------------------------------\n");
if(debug) {
	printf(" data block length: %08x (%d) bytes\n", (int) header.blocklength, (int) header.blocklength);
 	printf("    	  unknown1: %08x (%d)\n", (int) header.unknown1, (int) header.unknown1);
	printf("    	  unknown2: %08x (%d)\n", (int) header.unknown2, (int) header.unknown2);
	printf("          unknown3: %08x (%d)\n", (int) header.unknown3, (int) header.unknown3);
	printf("     timebase code: %08x (%d)\n", (int) header.timebasecode, (int) header.timebasecode);
	printf("    	  unknown4: %08x (%d)\n", (int) header.unknown4, (int) header.unknown4);
	printf("    vert sens code: %08x (%d)\n", (int) header.vertsenscode, (int) header.vertsenscode);
	printf("    	  unknown5: %08x (%d)\n", (int) header.unknown5, (int) header.unknown5);
	printf("          unknown6: %08x (%d)\n", (int) header.unknown6, (int) header.unknown6);
	printf("    	  unknown7: %08x (%d)\n", (int) header.unknown7, (int) header.unknown7);
    printf("          unknown8: %08x (%d)\n", (int) header.unknown8, (int) header.unknown8);
    printf("          unknown9: %08x (%d)\n", (int) header.unknown9, (int) header.unknown9);
    printf("\n");
	printf("-------------------------------\n");
	printf("\n");
}
	return header; // the length of this data block in bytes
}

// add the device locks to an array.
void found_usb_lock(struct usb_device *dev) {
  if(locksFound < MAX_USB_LOCKS) {
    usb_locks[locksFound++] = dev;
  }
}

struct usb_device *devfindOwon() {

	  struct usb_bus *bus;
	  struct usb_dev_handle *dh;
	  int buses = 0;
	  int devs = 0;

	  buses = usb_find_busses();
	  devs = usb_find_devices();

	  for (bus = usb_busses; bus; bus = bus->next) {
		struct usb_device *dev;
	    for (dev = bus->devices; dev; dev = dev->next)
	      if(dev->descriptor.idVendor == USB_LOCK_VENDOR && dev->descriptor.idProduct == USB_LOCK_PRODUCT) {
	        found_usb_lock(dev);
	        printf("..Found an Owon device %04x:%04x on bus %s\n", USB_LOCK_VENDOR,USB_LOCK_PRODUCT, bus->dirname);
	    	printf("..Resetting device.\n");
	    	dh=usb_open(dev);
			usb_reset(dh);	// quirky.. device has to be initially reset
	    	return(dev);	// return the device
	      }
	  }
	  return (NULL);		// No Owon found
}

void writeRawData(char *buf, int count) {
	FILE *fp;

	if ((fp=fopen(filename,"w")) == NULL) {
	  printf("..Failed to open file \'%s\'!\n", filename);
	  return;
	}
	else
      printf("..Successfully opened file \'%s\'!\n", filename);

	if (fwrite(buf,sizeof(unsigned char), count, fp) != count)
	  printf("..Failed to write %d bytes to file %s\n", count, filename);
	else
	  printf("..Successfully written %d bytes to file %s\n", count, filename);

	fclose(fp);
	return;
}

void writeTextData(char *buf, int count) {
	FILE *fp;
	char *ptr[channelcount];
	int i,j;
	char txtfilename[strlen(filename)+3];
	strcpy(txtfilename,filename);
	strcat(txtfilename,".txt");
	if ((fp=fopen(txtfilename,"w")) == NULL) {
	  printf("..Failed to open file \'%s\'!\n", txtfilename);
	  return;
	}
	else
		printf("..Successfully opened text file \'%s\'!\n", txtfilename);

	fprintf(fp,"# Units:(mV)\n");

	ptr[0] = buf + VECTORGRAM_FILE_HEADER_LENGTH;	// +10 bytes
	ptr[0] += VECTORGRAM_BLOCK_HEADER_LENGTH;		// +51 bytes

	for(i=1; i < channelcount; i++)
		ptr[i] = ptr[i-1] + headers[i-1].blocklength + 3;
	fprintf(fp, "#");
	for(i=0; i< channelcount; i++)
		fprintf(fp, "\t\t  %s", headers[i].channelname);
	fprintf(fp,"\n");

// for the sake of pointer sanity, we must check the blocklength of every channel..
// before trying to print a sample for channel 'n' for every timeslot up to the blocklength of channel 1
// Even so, this is less than ideal where the timebases for each channel are different... it means
// the plots produced from the data tables are basically meaningless

	for(j=0;j<headers[0].blocklength/2;j++) {
		fprintf(fp, "%d", j);
		for(i=0;i<channelcount;i++) {
			if(j>headers[i].blocklength/2)	// no sample available for this timeslot on channel i
				fprintf(fp,"\t\t    -");
			else
				fprintf(fp, "\t\t%5.1f", (float)((int) *ptr[i] * headers[i].vertSensitivity / 25));
			ptr[i]+=2;
		}
	fprintf(fp, "\n");
	}
	if(!fclose(fp))
		printf("..Successfully closed text file \'%s\'!\n", txtfilename);
}

void readOwonMemory(struct usb_device *dev) {

	usb_dev_handle *devHandle = 0;

	signed int ret=0;	// set to < 0 to indicate USB errors
	int i=0, j=0;

	unsigned int owonDataBufferSize=0;
	char owonCmdBuffer[0x0c];
	char owonDescriptorBuffer[0x12];
	char *owonDataBuffer;	 				 // malloc-ed at runtime
	char *headerptr;						 // used to reference the start of the header

	if(dev->descriptor.idVendor != USB_LOCK_VENDOR || dev->descriptor.idProduct != USB_LOCK_PRODUCT) {
	  printf("..Failed device lock attempt: not passed a USB device handle!\n");
	  return;
	}
	printf("..Attempting USB lock on device  %04x:%04x\n",
			dev->descriptor.idVendor, dev->descriptor.idProduct);

	devHandle = usb_open(dev);

	if(devHandle > 0) {
	  printf("..Trying to claim interface 0 of %04x:%04x \n",
			dev->descriptor.idVendor, dev->descriptor.idProduct);

	  ret = usb_set_configuration(devHandle, DEFAULT_CONFIGURATION);
	  ret = usb_claim_interface(devHandle, DEFAULT_INTERFACE); // interface 0

	  ret = usb_clear_halt(devHandle, BULK_READ_ENDPOINT);
//	  ret = usb_set_altinterface(devHandle, DEFAULT_INTERFACE);

	  if(ret) {
        printf("..Failed to claim interface %d: %d : \'%s\'\n", DEFAULT_INTERFACE, ret, strerror(-ret));
	    goto bail;
      }
	}
	else {
	  printf("..Failed to open device..\'%s\'", strerror(-ret));
	  goto bail;
	}

	printf("..Successfully claimed interface 0 to %04x:%04x \n",
			dev->descriptor.idVendor, dev->descriptor.idProduct);

  	printf("..Attempting to get the Device Descriptor\n");

  	ret = usb_get_descriptor(devHandle, USB_DT_DEVICE, 0x00, owonDescriptorBuffer, 0x12);

  	if(ret < 0) {
	  printf("..Failed to get device descriptor %04x '%s'\n", ret, strerror(-ret));
	  goto bail;
	}
	else
	  printf("..Successfully obtained device descriptor!\n");
/*
	printf("..Attempting to set device to default configuration\n");
		ret = usb_set_configuration(devHandle, DEFAULT_CONFIGURATION);
		if(ret) {
		  printf("..Failed to set default configuration %d '%s'\n", ret, strerror(-ret));
		  goto bail;
		}
*/

// clear any halt status on the bulk OUT endpoint
	ret = usb_clear_halt(devHandle, BULK_WRITE_ENDPOINT);

	printf("..Attempting to bulk write START command to device...\n");

	ret = usb_bulk_write(devHandle, BULK_WRITE_ENDPOINT, OWON_START_DATA_CMD,
			strlen(OWON_START_DATA_CMD), DEFAULT_TIMEOUT);

	if(ret < 0) {
	  printf("..Failed to bulk write %04x '%s'\n", ret, strerror(-ret));
	  goto bail;
	}
	printf("..Successful bulk write of %04x bytes!\n", strlen(OWON_START_DATA_CMD));

	// clear any halt status on the bulk IN endpoint
	ret = usb_clear_halt(devHandle, BULK_READ_ENDPOINT);

	printf("..Attempting to bulk read %04x (%d) bytes from device...\n", sizeof(owonCmdBuffer), sizeof(owonCmdBuffer));
	ret = usb_bulk_read(devHandle, BULK_READ_ENDPOINT, owonCmdBuffer,
			sizeof(owonCmdBuffer), DEFAULT_TIMEOUT);
	if(ret < 0) {
		usb_resetep(devHandle,BULK_READ_ENDPOINT);
		printf("..Failed to bulk read: %04x (%d) bytes: '%s'\n", sizeof(owonCmdBuffer), sizeof(owonCmdBuffer), strerror(-ret));
		goto bail;
	}
	else
	  printf("..Successful bulk read of %04x (%d) bytes! :\n", ret, ret);

if(debug) {
// display the contents of the BULK IN Owon Command Buffer
	printf("\t%08x: ",0);
	for(i=0; i<ret; i++)
      printf("%02x ", (unsigned char)owonCmdBuffer[i]);
    printf("\n");
}
// retrieve the bulk read byte count from the Owon command buffer
// the count is held in little endian format in the first 4 bytes of that buffer
    memcpy(&owonDataBufferSize,owonCmdBuffer,4); //  owonCmdBuffer, the source for memcpy, is little endian
//    printf("dataBufSize = %d\n", dataBufferSize);

    printf("..Attempting to malloc read buffer space of %08xh (%d) bytes\n", owonDataBufferSize, owonDataBufferSize);
    owonDataBuffer = malloc(owonDataBufferSize);
    if(!owonDataBuffer) {
      printf("..Failed to malloc(%08xh)!\n", owonDataBufferSize);
      goto bail;
    }
    else
      printf("..Successful malloc!\n");

    printf("..Owon ready to bulk transfer %08xh (%d) bytes\n", owonDataBufferSize, owonDataBufferSize);

	printf("..Attempting to bulk read %08xh (%d) bytes from device...\n", owonDataBufferSize, owonDataBufferSize);
	ret = usb_bulk_read(devHandle, BULK_READ_ENDPOINT, owonDataBuffer,
			owonDataBufferSize, DEFAULT_BITMAP_READ_TIMEOUT);
	if(ret < 0) {
	  printf("..Failed to bulk read: %xh (%d) bytes: %d - '%s'\n", owonDataBufferSize, owonDataBufferSize, ret, strerror(-ret));
	  usb_reset(devHandle);
	  goto bail;
	}
	else
	  printf("..Successful bulk read of %08xh (%d) bytes! : \n", ret, ret);

if (debug) {
// hexdump the first 0x30 bytes of the BULK IN Owon Data Buffer
	printf("..Hexdump of first 0x40 bytes of the BULK IN Owon Data Buffer :\n");
    for(i=0; i<=0x03; i++) {
      printf("\t%08x: ",i);
      for(j=0;j<0x10;j++)
    	printf("%02x ", (unsigned char) owonDataBuffer[(i*0x10)+j]);
      printf("\n");
    }
    printf("\n");
}
//determine from the header whether this is bitmap data or vectorgram

// is it a 'BM' (bitmap) ?
    if(*owonDataBuffer=='B' &&  *(owonDataBuffer+1)=='M')
        printf("640x480 bitmap of %04xh (%d) bytes\n", (int) *(owonDataBuffer+2), *(owonDataBuffer+2));

// is it a vectorgram ('SPBV') ?   If so, we decode the contents..

    else if(*owonDataBuffer=='S' &&  *(owonDataBuffer+1)=='P' &&
    		*(owonDataBuffer+2)=='B' &&  *(owonDataBuffer+3)=='V') {

    	printf("..Found vector gram data\n");
// initialise the header pointer to the first header in the data
    	headerptr = owonDataBuffer + VECTORGRAM_FILE_HEADER_LENGTH;	// jump over the "SPBV...." file header

    	while((owonDataBuffer+owonDataBufferSize) > headerptr) {
if (debug) {
    		// hexdump the first 0x50 bytes of channel header
    			printf("..Hexdump of channel header :\n");
    		    for(i=0; i<=0x05; i++) {
    		      printf("\t%08x: ",i);
    		      for(j=0;j<0x10;j++)
    		    	printf("%02x ", (unsigned char) *(headerptr+(i*10)+j));
    		      printf("\n");
    		    }
}
    		headers[channelcount] = decodeVectorgramBufferHeader(headerptr);
    		headerptr += headers[channelcount++].blocklength;
    		headerptr += 3; // and jump over the channel name itself
    	}
//        for(i=0;i<channelcount;i++)
//        	printf("%s has %d samples\n", headers[i].channelname, (int) headers[i].blocklength/2);
    }
// is it neither a BM (bitmap) nor a SPBV (vectorgram) ?
    else {
    	printf("..Failed to determine data type.\n");
		printf("%c %c %c %c\n", *owonDataBuffer, *(owonDataBuffer+1), *(owonDataBuffer+2), *(owonDataBuffer+3));
    }

// dump the buffer to disk file either as raw data or as tabulated text data as well.

    if(text)
    	writeTextData(owonDataBuffer, owonDataBufferSize);

    writeRawData(owonDataBuffer, owonDataBufferSize);

    free(owonDataBuffer);	// a buffer of vectorgrams is just a few KB in size
							// but for bitmaps this buffer could be very large (~1MB)

    printf("..Attempting to release interface %d\n", DEFAULT_INTERFACE);
    ret = usb_release_interface(devHandle, DEFAULT_INTERFACE);
    if(ret) {
      printf("Failed to release interface %d: '%s'\n", DEFAULT_INTERFACE, strerror(-ret));
	  goto bail;
    }
	printf("..Successful release of interface %d!\n", DEFAULT_INTERFACE);

bail:
	usb_reset(devHandle);
	usb_close(devHandle);
	return;
}

int main(int argc, char *argv[]) {

  if (argc>1) {
	  filename = malloc(strlen(argv[1]));
	  memcpy(filename, argv[1], strlen(argv[1]));
  }

  printf("..Initialising libUSB\n");
  usb_init();

  printf("..Searching USB buses for Owon\n");

  if(!devfindOwon())
	  printf("..No Owon device %04x:%04x found\n", USB_LOCK_VENDOR, USB_LOCK_PRODUCT);
  else
	readOwonMemory(usb_locks[0]);
//	for(i = 0; i < locksFound; i++)
//	  readOwonMemory(usb_locks[i]);	// if e
  return 0;
}
