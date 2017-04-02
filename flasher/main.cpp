/*#ifndef _WINDOWS
	#include "WinTypes.h"
#endif
#include "ftd2xx.h"
*/
#include <ftdi.h>

#include <stdio.h>
#include <string.h>
#include <string>

const int MAX_LINE_LEN = 80;
typedef unsigned char uchar;
typedef struct ftdi_context* FT_HANDLE;

char getCh( FT_HANDLE ftHandle );
void waitFor( FT_HANDLE ftHandle, const char ch );
void send( FT_HANDLE ftHandle, uchar ch );
std::string readHex(FILE *fd);
void uploadHex( FT_HANDLE ftHandle, const std::string &data );
int ParseHex( char *characters, int length );

// const int BOOTLOADER_START = 0x3A00; //TellStick
const int BOOTLOADER_START = 0x7A00; //TellStick Duo

int main(int argc, char **argv) {
	FILE *fd;
    int  ftStatus = 0;
    int i,ret = 0;
    char manufacturer[128], description[128];
    struct ftdi_device_list *devlist, *curdev;
    struct ftdi_context ftcx;
    FT_HANDLE ftHandle = &ftcx;

    int dwNumberOfDevices = 0;
//	int vid = 0x0403, pid = 0x6001;
    int vid = 0x1781, pid = 0x0C31;
//	int vid = 0x1781, pid = 0x0C30;

	if (argc < 2) {
		printf("Usage: %s filename\n", argv[0]);
		return 1;
	}

//	FT_SetVIDPID(vid, pid);
    if (ftdi_init( ftHandle )) {
        char *err = ftdi_get_error_string(ftHandle);
        fprintf(stderr,  "usb - init error: %s\n", err);
        return 1;
    }

	fd = fopen(argv[1], "r");
	if (!fd) {
//		fclose(fd);
		printf("Could not open file\n");
        ftdi_deinit( ftHandle );
		return 1;
	}

    bool found = false;
    if((ret = ftdi_usb_find_all(ftHandle, &devlist, vid, pid)) < 0) {
      fprintf(stderr, "ftdi_usb_find_all failed: %d (%s)\n", ret, ftdi_get_error_string(ftHandle));
      ftdi_deinit( ftHandle );
      return EXIT_FAILURE;
    }
    printf("Number of FTDI devices (telldus) found: %d\n", ret);
    i = 0;
    for (curdev = devlist; curdev != NULL; i++) {
      printf("Checking device: %d\n", i);
      if((ret = ftdi_usb_get_strings(ftHandle, curdev->dev, manufacturer, 128, description, 128, NULL, 0)) < 0) {
        fprintf(stderr, "ftdi_usb_get_strings failed: %d (%s)\n", ret, ftdi_get_error_string(ftHandle));
        return EXIT_FAILURE;
      }
      printf("Manufacturer: %s, Description: %s\n\n", manufacturer, description);
      found = true;
      ftStatus = ftdi_usb_open_dev(ftHandle, curdev->dev);
      break;
      curdev = curdev->next;
    }

    ftdi_list_free(&devlist);
    if (!found) {
        printf("Could not find TellStick (Duo)\n");
        return 1;
    }
/*
	ftStatus = FT_CreateDeviceInfoList(&dwNumberOfDevices);
	if (ftStatus == FT_OK) {
		bool found = false;
		for (int i = 0; i < (int)dwNumberOfDevices; i++) {

			FT_PROGRAM_DATA pData;
			char ManufacturerBuf[32];
			char ManufacturerIdBuf[16];
			char DescriptionBuf[64];
			char SerialNumberBuf[16];

			pData.Signature1 = 0x00000000;
			pData.Signature2 = 0xffffffff;
			pData.Version = 0x00000002;      // EEPROM structure with FT232R extensions
			pData.Manufacturer = ManufacturerBuf;
			pData.ManufacturerId = ManufacturerIdBuf;
			pData.Description = DescriptionBuf;
			pData.SerialNumber = SerialNumberBuf;

			ftStatus = FT_Open(i, &ftHandle);
			ftStatus = FT_EE_Read(ftHandle, &pData);
			if(ftStatus == FT_OK){
				if(pData.VendorId == vid && pData.ProductId == pid){
					found = true;
					break;
				}
			}
			FT_Close(ftHandle);
		}
		if (!found) {
			printf("Could not find TellStick (Duo)\n");
			return 1;
		}
    }*/


	if (pid == 0x0C31) {
        ftdi_set_baudrate(ftHandle, 115200);
	} else {
        ftdi_set_baudrate(ftHandle, 9600);
	}
    ftdi_setflowctrl(ftHandle, SIO_DISABLE_FLOW_CTRL);

	std::string data = readHex(fd);

    ftdi_usb_purge_buffers(ftHandle);

	printf("Reboot TellStick...");
    ftStatus = ftdi_set_bitmode(ftHandle, 0xff, 0x20);
	sleep(1);
	printf("Done\n");
    ftStatus = ftdi_set_bitmode(ftHandle, 0xf0, 0x20);

	printf("Waiting for TellStick Duo Bootloader.\n");
	waitFor(ftHandle, 'g');
	send(ftHandle, 'r');


	printf("Uploading hex-file\n");
	uploadHex(ftHandle, data);

	printf("Rebooting TellStick\n");
	waitFor(ftHandle, 'b');
 	send(ftHandle, 0x00);

	printf("Firmware updated!\n");
end:
    ftdi_usb_close(ftHandle);
    ftdi_deinit(ftHandle);
	fclose(fd);
}

char getCh( FT_HANDLE ftHandle ) {
	char buf = 0;
    int dwBytesRead = 0;
    //FT_Read(ftHandle, &buf, sizeof(buf), &dwBytesRead);
    dwBytesRead = ftdi_read_data(ftHandle, (unsigned char*)&buf, sizeof(buf));
	return buf;
}

void waitFor( FT_HANDLE ftHandle, const char ch ) {
	char buf = 0;

	while(1) {
		buf = getCh( ftHandle );
		if (ch == buf) {
			break;
		}
	}
}

void send( FT_HANDLE ftHandle, uchar ch ) {
    int bytesWritten;
    //FT_Write(ftHandle, &ch, sizeof(ch), &bytesWritten);
    bytesWritten = ftdi_write_data(ftHandle, &ch, sizeof(ch));
}

std::string readHex(FILE *fd) {
	char fileLine[MAX_LINE_LEN] = "";
	std::string data;
    char* ret = NULL;

	while( !feof(fd) ) {
        ret = fgets(fileLine, MAX_LINE_LEN, fd);

		if (fileLine[0] != ':' || strlen(fileLine) < 11) {
			// skip line if not hex line entry,or not minimum length ":BBAAAATTCC"
			continue;
		}

		int byteCount = ParseHex(&fileLine[1], 2);
		int startAddress = ParseHex(&fileLine[3], 4);
		int recordType = ParseHex(&fileLine[7], 2);

		if (recordType == 1) {
			//End of file, break
			break;
		}
		if (recordType == 2) {
			//Extended Segment Address Record. Not implemented yet
		} else if (recordType == 4) {
			//Extended Linear Address Record not supported
			break;
		} else if (recordType == 0) { //Data record
			if (strlen(fileLine) < (11+ (2*byteCount))) {
				// skip if line isn't long enough for bytecount.
				continue;
			}
			//Protect us from overwriting the bootloader
			if (startAddress >= BOOTLOADER_START) {
				continue;
			}
			//Pad with empty data when needed
			if ((startAddress > data.size())) {
				while (startAddress > data.size()) {
					data.append(1, 0xFF);
				}
			}
			for (int lineByte = 0; lineByte < byteCount; lineByte++) {
				uchar hex = (char)ParseHex(&fileLine[9 + (2 * lineByte)], 2);
				data.append(1, hex);
			}
		}
	}
	data.append(64, 0xff); //At least 64-bytes extra so the last block will be written to the memory
	return data;
}

void uploadHex( FT_HANDLE ftHandle, const std::string &data ) {
	int bytesLeft = 0, i = 0;
	char byte;
    int progress = 0;
	while (i < data.length()) {
		byte = getCh(ftHandle);
		if (byte == 'b') {
			bytesLeft = data.length() - i;
			if (bytesLeft > 0xFF) {
				bytesLeft = 0xFF;
			}
			send(ftHandle, bytesLeft);
		} else if (byte == 'd') {
			send(ftHandle, data[i]);
			--bytesLeft;
			++i;
            int tmp = (int)((float)i/data.length()*100.0);
            if (tmp  != progress) {
                printf("%d%%\n", progress);
                fflush(stdout);
                progress = tmp;
            }
		}
	}
	printf("\n100%%\n");
}

int ParseHex( char *characters, int length ) {
	int integer = 0;

	for (int i = 0; i < length; i++)
	{
		integer *= 16;
		switch(*(characters + i))
		{
			case '1':
				integer += 1;
				break;

			case '2':
				integer += 2;
				break;

			case '3':
				integer += 3;
				break;

			case '4':
				integer += 4;
				break;

			case '5':
				integer += 5;
				break;

			case '6':
				integer += 6;
				break;

			case '7':
				integer += 7;
				break;

			case '8':
				integer += 8;
				break;

			case '9':
				integer += 9;
				break;

			case 'A':
			case 'a':
				integer += 10;
				break;

			case 'B':
			case 'b':
				integer += 11;
				break;

			case 'C':
			case 'c':
				integer += 12;
				break;

			case 'D':
			case 'd':
				integer += 13;
				break;

			case 'E':
			case 'e':
				integer += 14;
				break;

			case 'F':
			case 'f':
				integer += 15;
				break;
		}
	}
	return integer;
}
