#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>

#define USB_VID_GATEWORKS	0x2beb
#define USB_PID_GWSOCK_BL	0x1100
#define USB_PID_GWSOCK_HID	0x1110

#include <hidapi/hidapi.h>

/* get a 32bit unsigned int from network and return in host order */
inline uint32_t get_uint32(uint8_t *buf)
{
	uint32_t val;

	val = buf[0];
	val |= buf[1] << 8;
	val |= buf[2] << 16;
	val |= buf[3] << 24;
	val = ntohl(val);

	return val;
}

/* output 32bit unsigned int in network byte order */
inline void set_uint32(uint8_t *buf, uint32_t val)
{
	val = htonl(val);
	buf[0] = val & 0xff;
	buf[1] = (val >> 8) & 0xff;
	buf[2] = (val >> 16) & 0xff;
	buf[3] = (val >> 24) & 0xff;
}

extern int psoc_program(hid_device *handle, const char *file);

enum command {
	SET_GPIO = 0x30,
	GET_GPIO = 0x31,
	SET_GPIO_DIR = 0x32,
	GET_GPIO_DIR = 0x33,
	SET_CONFIG = 0xf0,
	GET_CONFIG = 0xf1,
	BOOTLOADER = 0xff,
};

uint8_t buf[65];

hid_device *gwsoc_hid_open(const char* name, uint16_t vendorId,
			   uint16_t productId)
{
	struct hid_device_info *dev;
	hid_device *handle;

	/* Enumerate HID devices on the system */
	dev = hid_enumerate(vendorId, productId);
	if (!dev) {
		fprintf(stderr, "Error: %s %04x:%04x not found\n",
			name, vendorId, productId);
		return NULL;
	}

	/* display details */
	printf("%s: 0x%04x:0x%04x %ls %ls %ls\n", name,
		dev->vendor_id, dev->product_id,
		dev->manufacturer_string, dev->product_string,
		dev->serial_number);
	hid_free_enumeration(dev);

	/* open device */
	handle = hid_open(vendorId, productId, NULL);
	if (!handle)
		fprintf(stderr, "failed to open: %s\n", strerror(errno));

	return handle;
}

uint8_t *gwsoc_hid_read(hid_device *handle, int *len)
{
	int res;

	/* read data */
	memset(buf, 0, sizeof(buf));
	res = hid_read(handle, buf, sizeof(buf));
	if (res < 0) {
		fprintf(stderr, "hid_read failed: %ls\n", hid_error(handle));
		exit (1);
	}

	/* interpret data (network byte order) */
	switch(buf[0]) {
	case BOOTLOADER:
		printf("Exiting to bootloader\n");
		break;
	case GET_CONFIG:
	case SET_CONFIG:
		if (buf[4] == 0)
			printf("config%d=%s\n", buf[4], (char*) buf+8);
		else
			printf("config%d=0x%08x\n", buf[4], get_uint32(buf+8));
		break;
	case GET_GPIO_DIR:
	case SET_GPIO_DIR:
		printf("gpiodir=0x%08x\n", get_uint32(buf+4));
		break;
	case GET_GPIO:
	case SET_GPIO:
		printf("gpio=0x%08x\n", get_uint32(buf+4));
		break;
	}

	if (len)
		*len = res;
	return buf;
}

uint32_t gwsoc_hid_write(hid_device *handle, uint8_t cmd, uint32_t val,
	uint32_t val1)
{
	int res;

	/* set data (and perform any host to network byte order translations) */
	memset(&buf, 0, sizeof(buf));
	buf[0] = 0; /* report ID */
	buf[1] = cmd;
	switch(cmd) {
	case BOOTLOADER:
		break;
	case GET_CONFIG:
	case SET_CONFIG:
		buf[5] = val & 0xff;
		if (val == 0) /* serial# stored in ascii */
			sprintf(buf + 6, "%d", val1);
		else
			set_uint32(buf + 6, val1);
		break;
	case GET_GPIO_DIR:
	case SET_GPIO_DIR:
	case GET_GPIO:
	case SET_GPIO:
		set_uint32(buf + 5, val);
		break;
	}

	res = hid_write(handle, buf, sizeof(buf));
	if (res < 0) {
		fprintf(stderr, "hid_write failed: %ls\n", hid_error(handle));
		exit(1);
	}

	return res;
}

enum program_errors {
	err_gwsocwrite = 1,
	err_gettimeout,
	err_settimeout,
	err_bootloader,
	err_waitbootloader,
	err_program,
};

int program(const char *file)
{
	hid_device *handle;
	int res;

	handle = gwsoc_hid_open("GWSoC_HID", USB_VID_GATEWORKS,
				USB_PID_GWSOCK_HID);
	if (handle) {
		printf("Exiting Application\n");
		gwsoc_hid_write(handle, BOOTLOADER, 0, 0);
		sleep(1);
	}

	handle = gwsoc_hid_open("GWSoC Bootloader", USB_VID_GATEWORKS,
				USB_PID_GWSOCK_BL);
	if (!handle) {
		fprintf(stderr, "Failed waiting for bootloader\n");
		return -err_waitbootloader;
	}

	res = psoc_program(handle, file);
	if (res) {
		fprintf(stderr, "Failed programming: %d\n", res);
		return res;
	}

	hid_close(handle);

	return 0;
}

void usage(const char *pgm)
{
	fprintf(stderr, "usage: %s "
		" [-p filename]"
		" [gpio[=val]]"
		" [gpiodir[=val]]"
		" [config<n>[=<val>]]"
		"\n"
		, pgm);
	exit(1);
}

int main(int argc, char* argv[])
{
	int c;

	printf("GWSoC Utility v1.00\n");

	opterr = 0;
        while ((c = getopt(argc, argv, "p:h")) != -1) {
		switch (c) {
		case 'h':
			usage(argv[0]);
			break;
		case 'p':
			c = program(optarg);
			if (c)
				fprintf(stderr, "Failed programming: %d\n", c);
			return c;
			break;
		}
	}

	if (optind < argc) {
		char *cmd = argv[optind++];
		char *set = strchr(cmd, '=');
		uint32_t val;
		int len;

		if (set) {
			*set++ = 0;
			val = strtoll(set, NULL, 0);
		}

		/* GWSoC HID API */
		hid_device *handle = gwsoc_hid_open("GWSoC_HID",
			USB_VID_GATEWORKS, USB_PID_GWSOCK_HID);
		if (!handle)
			exit(1);

		if (strncasecmp(cmd, "gpiodir", 7) == 0) {
			if (set)
				gwsoc_hid_write(handle, SET_GPIO_DIR, val, 0);
			else
				gwsoc_hid_write(handle, GET_GPIO_DIR, 0, 0);
			gwsoc_hid_read(handle, &len);
		}

		else if (strncasecmp(cmd, "gpio", 4) == 0) {
			if (set)
				gwsoc_hid_write(handle, SET_GPIO, val, 0);
			else
				gwsoc_hid_write(handle, GET_GPIO, 0, 0);
			gwsoc_hid_read(handle, &len);
		}

		else if (strncasecmp(cmd, "config", 6) == 0) {
			int idx = 0xff;

			if (cmd[6])
				idx = atoi(cmd+6);
			if (set)
				gwsoc_hid_write(handle, SET_CONFIG, idx, val);
			else
				gwsoc_hid_write(handle, GET_CONFIG, idx, 0);
			gwsoc_hid_read(handle, &len);
		}

		else {
			usage(argv[0]);
		}
	} else {
		usage(argv[0]);
	}

	return 0;
}
