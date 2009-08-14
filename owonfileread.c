/*
 * owonfileread.c
 *				Read a binary (vectorgram) dump of the trace memory of an Owon scope from
 *				a local file. The vectorgram is parsed into a tabulated text format that
 *				can be plotted using GNUplot or a similar package.
 *
 * 				Copyright Aug 2009, Michael Murphy <ee07m060@elec.qmul.ac.uk>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <usb.h>
#include <string.h>
#include <asm/errno.h>
#include <arpa/inet.h> // for htonl() macro
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "owondump.h"

int debug = 0;							  // set to 1 for channel data hex dumps

struct usb_device *usb_locks[MAX_USB_LOCKS];
int locksFound = 0;

char *filename = "output.bin";			  // default output filename
int text = 1;							  // tabulated text output as well as raw data output
int channelcount = 0;					  // the number of channels in the data dump

struct channelHeader headers[10];		  // provide for up to ten scope channels

int decodeVertSensCode(long int i) {
// This is the one byte vertical sensitivity code
	char c = (char) i;
	int vertSensitivity=-1;
	switch (c) {
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
	char c = (char) i;
// return an LL int
	switch (c) {
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
		  default	: printf("..Unknown timebase code of %02x\n", c);
	}
//	printf("decodetimebase passed %02x and returned %Ld\n", c, timebase);
	return timebase;
}

// decode the contents of the vectorgram data header - providing us with the timebase and voltage values for the channel data
// hdrBuf has already been stripped of the 10 byte vectorgram file header that begins "SPB......"
// returns the size of the channel data block in bytes

struct channelHeader decodeVectorgramBufferHeader(char *hdrBuf) {
	struct channelHeader header;

// if not a bitmap then must be a vectorgram
	memcpy(&header.channelname,hdrBuf,3);
	header.channelname[3]='\0';
	memcpy(&header.blocklength, hdrBuf+3,4);
	memcpy(&header.samplecount1, hdrBuf+7,4);
	memcpy(&header.samplecount2, hdrBuf+11,4);
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
	printf("         sample count: %d\n", (int) header.samplecount1);
	printf(" vertical sensitivity: %dmV\n", header.vertSensitivity);
	printf("             timebase: %gms\n", (double) header.timeBase / 1000000);
	printf("-------------------------------\n");
if(debug) {
	printf(" data block length: %08x (%d) bytes\n", (int) header.blocklength, (int) header.blocklength);
 	printf("      samplecount1: %08x (%d)\n", (int) header.samplecount1, (int) header.samplecount1);
	printf("      samplecount2: %08x (%d)\n", (int) header.samplecount2, (int) header.samplecount2);
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
	return header;
}

void writeTextData(char *buf, int count) {
	FILE *fpout;
	char *ptr[channelcount];

	int i,j;
	char txtfilename[strlen(filename)+3];
	strcpy(txtfilename,filename);
	strcat(txtfilename,".txt");
	if ((fpout = fopen(txtfilename,"w")) == NULL) {
	  printf("..Failed to open file \'%s\'!\n", txtfilename);
	  return;
	}
	printf("..Successfully opened text file \'%s\'!\n", txtfilename);

	fprintf(fpout,"# Units:(mV) -- Timebase: (%gms)\n", (double) headers[0].timeBase / 1000000);

	ptr[0] = buf;									// initialise first pointer to start of char *buf
	ptr[0] += VECTORGRAM_FILE_HEADER_LENGTH;		// +10 bytes to jump over the file header
	ptr[0] += VECTORGRAM_BLOCK_HEADER_LENGTH;		// +51 bytes to jump over the channel header

//	fprintf(stderr, "ptr[%d] = 0x%p  (offset %d) \n", 0, ptr[0], (int) (ptr[0]-ptr[0]));

// print the channel names as column headers

	for(i=1; i < channelcount; i++) {
		ptr[i] = ptr[i-1] + (int) headers[i-1].blocklength + 3;
//		fprintf(stderr, "ptr[%d] = 0x%p  (offset %d) \n", i, ptr[i], (int) (ptr[i]-ptr[i-1]));
	}

	fprintf(fpout, "#");
	for(i=0; i < channelcount; i++)
		fprintf(fpout, "\t\t  %s", headers[i].channelname);
	fprintf(fpout,"\n");

// for the sake of pointer sanity, we must check the sample count of every channel..
// before trying to print a sample for channel 'n' for every timeslot up to the sample count of channel 1
//
// Even so, this is less than ideal where the timebases for each channel are different... it means
// the plots produced from the data tables are basically meaningless - but that's the way Owon have done it..
//
// see the README in the tarball for perhaps how the text data file should be written.

	for(j=0;j < (int) headers[0].samplecount1;j++) {
		fprintf(fpout, "%d", j+1);
		for(i = 0 ;i < channelcount;i++) {
			if(j > (int) headers[i].samplecount1)	// no sample available for this timeslot on channel i
				fprintf(fpout,"\t\t    -");
			else
				fprintf(fpout, "\t\t%5.1f", (short int) *ptr[i] * headers[i].vertSensitivity * 0.04);
			ptr[i]+=2;
		}
	fprintf(fpout, "\n");
	}
	printf("..Successfully written trace data to \'%s\'!\n", txtfilename);
	if(!fclose(fpout))
		printf("..Successfully closed text file \'%s\'!\n", txtfilename);
}

void readOwonBinFile(FILE *fp) {

	int i, j, ret;
	int owonFileSize=0;
	char *owonDataBuffer;	 				 // malloc-ed at runtime
	char *headerptr;						 // used to reference the start of the header
	int fd;
	struct stat buf;

	fd = fileno(fp);
	fstat(fd, &buf);

	owonFileSize = buf.st_size;

    printf("..Attempting to malloc read buffer space of %08xh (%d) bytes\n", owonFileSize, owonFileSize);
    owonDataBuffer = malloc(owonFileSize);
    if(!owonDataBuffer) {
      printf("..Failed to malloc(%08xh)!\n", owonFileSize);
      goto bail;
    }
    else
      printf("..Successful malloc!\n");

	printf("..Attempting to read %08xh (%d) bytes from file...\n", owonFileSize, owonFileSize);
	ret = fread(owonDataBuffer, sizeof(char), owonFileSize, fp);

	if(ret < 0) {
	  printf("..Failed to read: %xh (%d) bytes: %d - '%s'\n", owonFileSize, owonFileSize, ret, strerror(-ret));
	  goto bail;
	}
	else
	  printf("..Successful read of %08xh (%d) bytes! : \n", ret, ret);

if (debug) {
// hexdump the first 0x40 bytes of the Owon file Buffer
	printf("..Hexdump of first 0x40 bytes of the Owon binary file:\n");
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

    else if(*owonDataBuffer=='S' &&  *(owonDataBuffer+1)=='P' && *(owonDataBuffer+2)=='B') {
    	switch (*(owonDataBuffer+3)) {
			case 'V' :	printf("..Found data from Owon PDS5022S\n");
						break;
			case 'W' :	printf("..Found data from Owon PDS6062S\n");
						break;
			default	 :	printf("..Found data from Owon unknown model\n");
			}

    	printf("..Found vectorgram data\n");

// initialise the header pointer to the first header in the data

    	headerptr = owonDataBuffer + VECTORGRAM_FILE_HEADER_LENGTH;	// jump over the 10 byte "SPB...." file header

//    	printf("owonDataBuffer = %Ld\n", (long long int)owonDataBuffer);
//    	printf("headerptr = (oDB + FILE_HDR_LEN (%d) = %Ld\n", VECTORGRAM_FILE_HEADER_LENGTH,  (long long int)headerptr);
//    	printf("owonFileSize = %Ld\n", (long long int)owonFileSize);

    	while((owonDataBuffer+owonFileSize) > headerptr) {
if (debug) {
    		// hexdump the first 0x50 bytes of channel header
    			printf("..Hexdump of channel header :\n");
    		    for(i=0; i<=0x05; i++) {
    		      printf("\t%08x: ",i);
    		      for(j=0;j<0x10;j++)
    		    	printf("%02x ", (unsigned char) *(headerptr+(i*0x10)+j));
    		      printf("\n");
    		    }
}	// end if (debug)
    		headers[channelcount] = decodeVectorgramBufferHeader(headerptr);
    		headerptr += (int) headers[channelcount].blocklength;
    		headerptr += 3; // and jump over the channel name itself
    		channelcount++;
    	}
//        for(i=0;i<channelcount;i++)
//        	printf("%s has %d samples\n", headers[i].channelname, (int) headers[i].samplecount1); 	// (16 bits used per sample)
    }
// is it neither a BM (bitmap) nor a SPB (vectorgram) ?
    else {
    	printf("..Failed to determine data type.\n");
		printf("%c %c %c %c\n", *owonDataBuffer, *(owonDataBuffer+1), *(owonDataBuffer+2), *(owonDataBuffer+3));
    }

// dump the buffer to disk as tabulated text data.

    writeTextData(owonDataBuffer, owonFileSize);

    free(owonDataBuffer);	// a buffer of vectorgrams is just a few KB in size
							// but for bitmaps this buffer could be very large (~1MB)

bail:
	return;
}

int main(int argc, char *argv[]) {

  FILE *fp;

//  printf("..Size of short int=%d, int=%d, long int = %d,  long long int = %d \n", (int) sizeof(short int), (int) sizeof(int), (int) sizeof(long int), (int) sizeof(long long int));

  if (argc>1) {
	  filename = malloc(strlen(argv[1]));
	  memcpy(filename, argv[1], strlen(argv[1]));
  }
  else {
	  printf("..Usage: owonfileread owonbinary filename\n");
	  return 0;
  }

  if ((fp = fopen(filename, "r")) == NULL) {
	  printf("..Couldn\'t open %s\n", filename);
	  return 0;
  }

  readOwonBinFile(fp);
  fclose(fp);
  return 0;
}
