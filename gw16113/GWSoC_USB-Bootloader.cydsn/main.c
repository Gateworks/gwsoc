/*****************************************************************************
* File Name: main.c
*
* GW16113 Bootloader
*****************************************************************************/
#include <device.h>

enum blmode {
	BLMODE_WAIT_FOR_CMD = 0,
	BLMODE_WAIT_IF_MAGIC = 1,
	BLMODE_JUMP_TO_APP = 2,
};
struct config
{
	uint8 size;
	uint8 version;
	uint16 chksum; /* checksum of all following bytes */

	uint8 serial[8];
	uint32 blmode;
	uint32 blmask;
	uint32 blmagic;
};
struct config cfg;
uint8 serialnum[32];

void load_config() {
	uint8* eeprom = (uint8*)CYDEV_EE_BASE;
	int i;

	/* read config to RAM then turn off EEPROM (to save power) */
	EEPROM_Start();
	CyDelayUs(5); /* per datasheet */
	memcpy(&cfg, eeprom, sizeof(cfg));
	EEPROM_Stop(); /* turn off EEPROM to conserve power */

	/* Set serial string */
	for (i = 0; i < sizeof(cfg.serial); i++)
		serialnum[2 + (i*2)] = cfg.serial[i];
	serialnum[0] = 2 + sizeof(cfg.serial)*2; /* DescriptorLen */
	serialnum[1] = 0x3u; /* DescriptorType: STRING */
	USBFS_SerialNumString(serialnum);
}

void main()
{
	uint8_t wait = 0;
	uint32_t strapping = CY_GET_REG8(CYREG_PRT12_PS) |
			     (CY_GET_REG8(CYREG_PRT3_PS) << 8) |
			     (CY_GET_REG8(CYREG_PRT0_PS) << 16);

	load_config();

	switch(cfg.blmode) {
	case BLMODE_WAIT_FOR_CMD:
		wait = 1;
		break;
	case BLMODE_WAIT_IF_MAGIC:
	      if ((strapping & cfg.blmask) == cfg.blmagic)
			wait = 1;
		break;
	case BLMODE_JUMP_TO_APP:
		wait = 0;
		break;
	}

	/* If strapping pattern, or unprogrammed wait in bootloader */
	if (wait) {
		BL_SET_RUN_TYPE(BL_START_BTLDR);
	}

	/* Start Bootloader - we never return from this function */
	CyBtldr_Start();

	/* CyGlobalIntEnable; */ /* Uncomment to enable global interrupts. */
	for(;;) {
		/* Place your application code here. */
	}
}
