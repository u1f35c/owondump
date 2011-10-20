/*
 * owondump.c  0.3		A userspace USB driver that interrogates an Owon PDS digital oscilloscope
 * 				and gets it to dump its trace memory for all channels, in both vectorgram
 * 				format, and/or in .BMP bitmap format.
 *
 * 				The vectorgram is also parsed into a tabulated text format that can be
 * 				plotted using GNUplot or a similar package.
 *
 * 				The code now has an elegant decodeTimebase function, courtesy of Michel Poullet!
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

int debug = 0;							  // set to 1 for channel data hex dumps

struct usb_device *usb_locks[MAX_USB_LOCKS];
int locksFound = 0;

char *filename = "output.bin";			  // default output filename
int text = 1;							  // tabulated text output as well as raw data output
int channelcount = 0;					  // the number of channels in the data dump


struct channelHeader headers[10];		  // provide for up to ten scope channels

int decodeVertSensCode(int sens_code, int probex_code) {
	int vertSensitivity=-1;
	switch (sens_code) {
	  case 0x01 : vertSensitivity = 5;		// 5mV
				  break;
	  case 0x02 : vertSensitivity = 10;
				  break;
	  case 0x03 : vertSensitivity = 20;
				  break;
	  case 0x04 : vertSensitivity = 50;
	  			  break;
	  case 0x05 : vertSensitivity = 100;
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

	switch (probex_code) {
	  case 0x00 : vertSensitivity *= 1;		// x1
				  break;
	  case 0x01 : vertSensitivity *= 10;            // x10
				  break;
	  case 0x02 : vertSensitivity *= 100;
				  break;
	  case 0x03 : vertSensitivity *= 1000;
        }

	return vertSensitivity;
}


uint32_t get_uint32(char *p) {
	uint32_t a;
	memcpy(&a, p,4); // TODO: fix big endian issue
	return a;
}

// decode the contents of the vectorgram data header - providing us with the timebase and voltage values for the channel data
// hdrBuf has already been stripped of the 10 byte vectorgram file header that begins "SPBV......"
// returns the size of the channel data block in bytes

struct channelHeader decodeVectorgramBufferHeader(char *hdrBuf) {
	struct channelHeader header;

// if not a bitmap then must be a vectorgram
	memcpy(&header.channelname,hdrBuf,3);
	header.channelname[3]='\0';
	header.blocklength = get_uint32(hdrBuf+3);
	header.samplecount1 = get_uint32(hdrBuf+7);
	header.samplecount2 = get_uint32(hdrBuf+11);
	header.startoffset = get_uint32(hdrBuf+15);
	header.timebasecode = get_uint32(hdrBuf+19);
	header.v_position = (int)get_uint32(hdrBuf+23);
	header.vertsenscode = get_uint32(hdrBuf+27);
	header.probexcode = get_uint32(hdrBuf+31);

	memcpy(&header.t_sample, hdrBuf+35,4);
	memcpy(&header.unknown7, hdrBuf+39,4);
	memcpy(&header.unknown8, hdrBuf+43,4);
	memcpy(&header.unknown9, hdrBuf+43,4);


	header.vertSensitivity = decodeVertSensCode(header.vertsenscode, header.probexcode);	// 5mV through 5000mV (5V)
	header.samplePerDiv = header.samplecount1/10;
	header.timeBase = header.t_sample * header.samplePerDiv * 1000;		    	// in nanoseconds (10E-9)
	printf("Channel: %4s samples: %6u sensitivity: %6u mV timebase: %g us (code %u) t_sample: %g us\n", 
		header.channelname,
		header.samplecount1,
		header.vertSensitivity,
		header.timeBase / 1000, 
		header.timebasecode,
		header.t_sample);
if(debug) {
	printf(" data block length: %08x (%u) bytes\n", header.blocklength, header.blocklength);
 	printf("      samplecount1: %08x (%u)\n", header.samplecount1, header.samplecount1);
	printf("      samplecount2: %08x (%u)\n", header.samplecount2, header.samplecount2);
	printf("      sampleoffset: %08x (%u)\n", header.startoffset, header.startoffset);
	printf("     timebase code: %08x (%u)\n", header.timebasecode, header.timebasecode);
	printf("    	v_position: %08x (%d)\n", header.v_position, header.v_position);
	printf("    vert sens code: %08x (%u)\n", header.vertsenscode, header.vertsenscode);
	printf("   probe mult code: %08x (%u)\n", header.probexcode, header.probexcode);
	printf("          t_sample: %g us\n", header.t_sample);
	printf("    	  unknown7: %g\n", header.unknown7);
    printf("          unknown8: %g\n", header.unknown8);
    printf("          unknown9: %g\n", header.unknown9);
    printf("\n");
	printf("-------------------------------\n");
	printf("\n");
}
	return header;
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
//	    	printf("..Resetting device.\n");
	    	dh=usb_open(dev);
			usb_reset(dh);	// quirky.. device has to be initially reset
	    	return(dev);	// return the device
	      }
	  }
	  return (NULL);		// No Owon found
}

void writeRawData(const unsigned char *buf, int count) {
	FILE *fp;

	if ((fp=fopen(filename,"w")) == NULL) {
	  printf("..Failed to open file \'%s\'!\n", filename);
	  return;
	}
//	else 
//      printf("..Successfully opened file \'%s\'!\n", filename);

	if (fwrite(buf,sizeof(unsigned char), count, fp) != count)
	  printf("..Failed to write %d bytes to file %s\n", count, filename);
//	else
//	  printf("..Successfully written %d bytes to file %s\n", count, filename);

	fclose(fp);
	return;
}


void writeTextData(const unsigned char *buf, int count) {
	FILE *fpout;
	const unsigned char *ptr[channelcount];
	unsigned int  offset[channelcount], n_samples;
	int i,j;
	double time = 0;
	char txtfilename[strlen(filename)+3];

	strcpy(txtfilename,filename);
	strcat(txtfilename,".txt");
	if ((fpout = fopen(txtfilename,"w")) == NULL) {
	  printf("..Failed to open file \'%s\'!\n", txtfilename);
	  return;
	}
//	printf("..Successfully opened text file \'%s\'!\n", txtfilename);

	fprintf(fpout,"# Timebase: %g us Samples: %u t_sample: %g us\n",
		headers[0].timeBase / 1000,
		headers[0].samplecount1,
		headers[0].t_sample);

	ptr[0] = buf;									// initialise first pointer to start of char *buf
	ptr[0] += VECTORGRAM_FILE_HEADER_LENGTH;		// +10 bytes to jump over the file header
	ptr[0] += VECTORGRAM_BLOCK_HEADER_LENGTH;		// +51 bytes to jump over the channel header

	n_samples = headers[0].samplecount2;

//	fprintf(stderr, "ptr[%d] = 0x%p  (offset %d) \n", 0, ptr[0], (int) (ptr[0]-ptr[0]));

	for(i=1; i < channelcount; i++) {
		if (n_samples < headers[i].samplecount2)
			n_samples = headers[i].samplecount2;
		ptr[i] = ptr[i-1] + headers[i-1].blocklength + 3;
//		fprintf(stderr, "ptr[%d] = 0x%p  (offset %d) \n", i, ptr[i], (int) (ptr[i]-ptr[i-1]));
	}

// print the channel names as column headers

	fprintf(fpout, "# ");
	for(i=0; i < channelcount; i++) {
		offset[i] = headers[i].startoffset;
		if (offset[i] != 0 && headers[i].samplecount1 == headers[i].samplecount2)
			offset[i]++; // this adjustment is very strange - but needed...
		fprintf(fpout, "%s\t", headers[i].channelname);
	}
	fprintf(fpout,"\n");

	for(j=0;j < n_samples;j++) {
		//fprintf(fpout, "%d", j+1);
		//fprintf(fpout, "%g\t", time);
		for(i = 0 ;i < channelcount;i++) {
			if(j > headers[i].samplecount2)	// no sample available for this timeslot on channel i
				fprintf(fpout,"    -\t");
			else {
				int n = offset[i] % headers[i].samplecount2;
				int s = (short)(ptr[i][n*2] | (ptr[i][n*2+1] << 8));
				fprintf(fpout, "%5.1f\t",  s * headers[i].vertSensitivity * 0.04);
			}
			offset[i]++;
			time += headers[0].t_sample;
		}
	fprintf(fpout, "\n");
	}
//	printf("..Successfully written trace data to \'%s\'!\n", txtfilename);
	if(!fclose(fpout))
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
//	printf("..Attempting USB lock on device  %04x:%04x\n",
//			dev->descriptor.idVendor, dev->descriptor.idProduct);

	devHandle = usb_open(dev);

	if(devHandle > 0) {
//	  printf("..Trying to claim interface 0 of %04x:%04x \n",
//			dev->descriptor.idVendor, dev->descriptor.idProduct);

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

//	printf("..Successfully claimed interface 0 to %04x:%04x \n",
//			dev->descriptor.idVendor, dev->descriptor.idProduct);

//  	printf("..Attempting to get the Device Descriptor\n");

  	ret = usb_get_descriptor(devHandle, USB_DT_DEVICE, 0x00, owonDescriptorBuffer, 0x12);

  	if(ret < 0) {
	  printf("..Failed to get device descriptor %04x '%s'\n", ret, strerror(-ret));
	  goto bail;
	}
	else
//	  printf("..Successfully obtained device descriptor!\n");
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

//	printf("..Attempting to bulk write START command to device...\n");

	ret = usb_bulk_write(devHandle, BULK_WRITE_ENDPOINT, OWON_START_DATA_CMD,
			strlen(OWON_START_DATA_CMD), DEFAULT_TIMEOUT);

	if(ret < 0) {
	  printf("..Failed to bulk write %04x '%s'\n", ret, strerror(-ret));
	  goto bail;
	}
//	printf("..Successful bulk write of %04x bytes!\n", (unsigned int) strlen(OWON_START_DATA_CMD));

	// clear any halt status on the bulk IN endpoint
	ret = usb_clear_halt(devHandle, BULK_READ_ENDPOINT);

//	printf("..Attempting to bulk read %04x (%d) bytes from device...\n",(unsigned int) sizeof(owonCmdBuffer), (unsigned int)  sizeof(owonCmdBuffer));
	ret = usb_bulk_read(devHandle, BULK_READ_ENDPOINT, owonCmdBuffer,
			sizeof(owonCmdBuffer), DEFAULT_TIMEOUT);
	if(ret < 0) {
		usb_resetep(devHandle,BULK_READ_ENDPOINT);
		printf("..Failed to bulk read: %04x (%d) bytes: '%s'\n", (unsigned int) sizeof(owonCmdBuffer),(unsigned int)  sizeof(owonCmdBuffer), strerror(-ret));
		goto bail;
	}
//	else
//	  printf("..Successful bulk read of %04x (%d) bytes! :\n", ret, ret);

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
//    printf("dataBufSize = %d\n", owonDataBufferSize);

//    printf("..Attempting to malloc read buffer space of %08xh (%d) bytes\n", owonDataBufferSize, owonDataBufferSize);
    owonDataBuffer = malloc(owonDataBufferSize);
    if(!owonDataBuffer) {
      printf("..Failed to malloc(%08xh)!\n", owonDataBufferSize);
      goto bail;
    }
//    else
//      printf("..Successful malloc!\n");

//    printf("..Owon ready to bulk transfer %08xh (%d) bytes\n", owonDataBufferSize, owonDataBufferSize);

//	printf("..Attempting to bulk read %08xh (%d) bytes from device...\n", owonDataBufferSize, owonDataBufferSize);
	ret = usb_bulk_read(devHandle, BULK_READ_ENDPOINT, owonDataBuffer,
			owonDataBufferSize, DEFAULT_BITMAP_READ_TIMEOUT);
	if(ret < 0) {
	  printf("..Failed to bulk read: %xh (%d) bytes: %d - '%s'\n", owonDataBufferSize, owonDataBufferSize, ret, strerror(-ret));
	  usb_reset(devHandle);
	  goto bail;
	}
//	else
//	  printf("..Successful bulk read of %08xh (%d) bytes! : \n", ret, ret);

if (debug) {
// hexdump the first 0x40 bytes of the BULK IN Owon Data Buffer
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
        printf("640x480 bitmap of %04xh (%u) bytes\n", *(owonDataBuffer+2), *(owonDataBuffer+2));

// is it a vectorgram ('SPB') ?   If so, we decode the contents..

    else if(*owonDataBuffer=='S' &&  *(owonDataBuffer+1)=='P' && *(owonDataBuffer+2)=='B') {
     	switch (*(owonDataBuffer+3)) {
			case 'V' :	printf("..Found data from Owon PDS5022S\n");
						break;
			case 'W' :	printf("..Found data from Owon PDS6060S\n");
						break;
			case 'X' :	printf("..Found data from Owon PDS7102T\n");
						break;
			default	 : 	printf("..Found data from Owon unknown model\n");
     	}

    	printf("..Found vector gram data\n");
// initialise the header pointer to the first header in the data
    	headerptr = owonDataBuffer + VECTORGRAM_FILE_HEADER_LENGTH;	// jump over the "SPB...." file header
/*
    	printf("!!! owonDataBuffer = 0x%p\n", owonDataBuffer);
    	printf("!!! owonDataBufferSize = %x (%d)\n", owonDataBufferSize, owonDataBufferSize);
    	printf("!!! VECTORGRAM_FILE_HDR_LENGTH= %02x\n", VECTORGRAM_FILE_HEADER_LENGTH);
    	printf("!!! headerptr = 0x%p\n", headerptr);
    	printf("!!! owondatabuffer + owonDataBufferSize = 0x%p    headerptr = 0x%p\n", owonDataBuffer+owonDataBufferSize, headerptr);
    	printf("!!! owondatabuffer + owonDataBufferSize (0x%p) > headerptr (0x%p) = %d\n", owonDataBuffer+owonDataBufferSize , headerptr, owonDataBuffer+owonDataBufferSize > headerptr);
*/
    	while( (owonDataBuffer + owonDataBufferSize) > headerptr) {
 if (debug) {
    		// hexdump the first 0x40 bytes of channel header
    			printf("..Hexdump of channel header :\n");
    		    for(i=0; i<=0x04; i++) {
    		      printf("\t%08x: ",i);
    		      for(j=0;j<0x10;j++)
    		    	printf("%02x ", (unsigned char) *(headerptr+(i*0x10)+j));
    		      printf("\n");
    		    }
 }
    		headers[channelcount] = decodeVectorgramBufferHeader(headerptr);
    		headerptr += headers[channelcount].blocklength;
    		headerptr += 3; 									// and jump over the channel name itself
    		channelcount++;
    	}
//        for(i=0;i<channelcount;i++)
//        	printf("%s has %u samples\n", headers[i].channelname, headers[i].blocklength/2);
    }
// is it neither a BM (bitmap) nor a SPB (vectorgram) ?
    else {
    	printf("..Failed to determine data type.\n");
		printf("%c %c %c %c\n", *owonDataBuffer, *(owonDataBuffer+1), *(owonDataBuffer+2), *(owonDataBuffer+3));
    }

// dump the buffer to disk file either as raw data or as tabulated text data as well.

    if(text)
    	writeTextData((const unsigned char*)owonDataBuffer, owonDataBufferSize);

    writeRawData((const unsigned char*)owonDataBuffer, owonDataBufferSize);

    free(owonDataBuffer);	// a buffer of vectorgrams is just a few KB in size
							// but for bitmaps this buffer could be very large (~1MB)

//    printf("..Attempting to release interface %d\n", DEFAULT_INTERFACE);
    ret = usb_release_interface(devHandle, DEFAULT_INTERFACE);
    if(ret) {
      printf("Failed to release interface %d: '%s'\n", DEFAULT_INTERFACE, strerror(-ret));
	  goto bail;
    }
//	printf("..Successful release of interface %d!\n", DEFAULT_INTERFACE);

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

//  printf("..Initialising libUSB\n");
  usb_init();

//  printf("..Searching USB buses for Owon\n");

  if(!devfindOwon()) {
	  printf("..No Owon device %04x:%04x found\n", USB_LOCK_VENDOR, USB_LOCK_PRODUCT);
	  return 0;
  }
  else
	readOwonMemory(usb_locks[0]);
//	for(i = 0; i < locksFound; i++)
//	  readOwonMemory(usb_locks[i]);
  return 0;
}
