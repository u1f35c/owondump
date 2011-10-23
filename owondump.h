// owondump.h - linux USB userspace driver for the owon PDS digital storage scopes
// Copyright 2009 Michael Murphy <ee07m060@elec.qmul.ac.uk>

#define USB_LOCK_VENDOR 0x5345			  // Dev : (5345) Owon Technologies
#define USB_LOCK_PRODUCT 0x1234			  // 	   (1234) PDS Digital Oscilloscope
#define OWON_START_DATA_CMD "START"
#define BULK_WRITE_ENDPOINT 0x03
#define BULK_READ_ENDPOINT 0x81
#define DEFAULT_INTERFACE 0x00
#define DEFAULT_CONFIGURATION 0x01
#define DEFAULT_TIMEOUT	500				  // 500mS for USB timeouts
#define DEFAULT_BITMAP_READ_TIMEOUT 3000  // allow Owon the extra time needed to fill USB buffer for bitmap data
#define MAX_USB_LOCKS 10				  // allow multiple scopes to slave to same PC host
#define MAX_HEADER_LENGTH 0x40
#define VECTORGRAM_FILE_HEADER_LENGTH 10  // for vectorgrams, the data header begins 10 bytes after file header
#define VECTORGRAM_BLOCK_HEADER_LENGTH 51
#define VECTORGRAM_BLOCK_HEADER_CHNAMELEN 3	// "CH1", "CH2", "CHA", etc.

// this is the structure of the header that precedes each block of channel data sent by the Owon

struct channelHeader {
	char channelname[4];	// 3 bytes ( {"CH1", "CH2", "CHA", "CHB", "CHC", "CHD"} )
	unsigned blocklength;	// 4 bytes (little endian long int holding data block length)
	unsigned samplecount1;	// 4 bytes (little endian long int holding total number of samples)
	unsigned samplecount2;	// 4 bytes (little endian long int holding the number of used samples)
	unsigned startoffset;	// 4 bytes (offset of first used sample (actually last?)
	unsigned timebasecode;	// 4 bytes (little endian long int holding timebase code - 0x00000000 (5ns) to 0x000000ff (100s)
	int      v_position;	// 4 bytes (vertical position in screen coordinates)
	unsigned vertsenscode;	// 4 bytes (little endian long int holding vertical sensitivity code - 0x00000001 (5mV) to 0x0000000A (5V)
	unsigned probexcode;	// 4 bytes (probe mult code 0: x1, 1: x10, 2: x100...)
	float    t_sample;	// 4 bytes time between samples (us)
	float    frequency;	// 4 bytes frequency of signal (Hz)
	float    period;	// 4 bytes period of signal (us)
	float    unknown9;	// 4 bytes (purpose unknown)

// these last three should be in a separate structure since they are not contained in the binary header
	int vertSensitivity;	// 5mV through 5000mV (5V)
	unsigned samplePerDiv;	// samples per division (something like 100 to 625)
	double timeBase;        // in nanoseconds (10E-9)
};

