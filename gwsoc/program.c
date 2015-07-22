#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <hidapi/hidapi.h>

#include "cybtldr_api.h"
#include "cybtldr_api2.h"

static hid_device *handle = NULL;

static int CyBtldr_Open(void)
{
	return 0;
}

static int CyBtldr_Close(void)
{
	return 0;
}

static int CyBtldr_Read(unsigned char* data, int count)
{
	unsigned char buf[65];
	int res;

	buf[0] = 0; // Report ID
	res = hid_read(handle, buf, count);
	if (res < 0) {
		fprintf(stderr, "USB HID Read Failure: %ls\n",
			hid_error(handle));
	}
	memcpy(data, buf, count);

	return (res >= 0) ? 0 : -1;
}

static int CyBtldr_Write(unsigned char* data, int count)
{
	unsigned char buf[65];
	int i, res;

	buf[0] = 0; // report ID
	for (i = 0; i < count; ++i)
		buf[i+1] = data[i];

	res = hid_write(handle, buf, count + 1);
	if (res < 0) {
		fprintf(stderr, "USB HID Write Failure: %ls\n",
			hid_error(handle));
	}

	return (res >= 0) ? 0 : -1;
}

static void CyBtldr_Progress(unsigned char arrayId, unsigned short rowNum)
{
	printf(".");
	fflush(stdout);
}

int psoc_program(hid_device *_handle, const char *file)
{
	int res;
	CyBtldr_CommunicationsData cyComms = {
		&CyBtldr_Open,
		&CyBtldr_Close,
		&CyBtldr_Read,
		&CyBtldr_Write,
		64,
	};

	printf("Programming %s", file);
	handle = _handle;
	res = CyBtldr_Program(file, &cyComms, CyBtldr_Progress);
	if (res)
		fprintf(stderr, "Failed\n");
	else
		printf("\nProgramming complete\n");

	return res;
}
