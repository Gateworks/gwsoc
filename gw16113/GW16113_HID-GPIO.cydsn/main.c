/*****************************************************************************
* File Name: main.c
*
* GW16113 Application
*****************************************************************************/

#include <device.h>

/* USB Endpoints */
enum USB_ENDPOINTS
{
	USB_EP_IN = 1,
	USB_EP_OUT = 2,
};

enum USB_STATE
{
	USB_IDLE,
	USB_DATA_SENT
};
int usbInEpState;
uint8 IN_Data_Buffer[64];
uint8 OUT_Data_Buffer[64];
uint8 usbReady;
uint8 serialnum[32];
extern volatile uint8 USBFS_deviceAddress;

/* EEPROM Configuration */
enum blmode {
        BLMODE_WAIT_FOR_CMD = 0,
        BLMODE_WAIT_IF_MAGIC = 1,
        BLMODE_JUMP_TO_APP = 2,
};
struct config
{
	uint8 size;
	uint8 version; /* version info */
	uint16 chksum;

	/* Configuration Items */
	uint8 serial[8];	/* config0 */
	uint32 blmode;		/* config1 */
	uint32 blmask;		/* config2 */
	uint32 blmagic;		/* config3 */

	/* Initial Port I/O Configuration */
	uint32 direction;	/* config4 */
	uint8 drivemode[24];	/* config5-29 */
};
struct config cfg;

/* USB HID API */
enum command {
	SET_GPIO = 0x30,
	GET_GPIO = 0x31,
	SET_GPIO_DIR = 0x32,
	GET_GPIO_DIR = 0x33,

	SET_CONFIG = 0xf0,
	GET_CONFIG = 0xf1,
	SET_TEST = 0xfd,
	GET_TEST = 0xfe,
	BOOTLOADER = 0xff,
};

enum drive_mode {
	DM_PU = 0x00,
	DM_PD = 0x01,
	DM_OD = 0x02,
	DM_TP = 0x03,
};

/* get 32bit uint in network byte order and return in host order */
uint32_t get_uint32(uint8_t *buf) {
	return
		(buf[3] |
		 (buf[2] << 8) |
		 (buf[1] << 16) |
		 (buf[0] << 24));
}

/* put 32bit uint from host order to network byte order */
void set_uint32(uint8_t *buf, uint32_t val) {
	buf[3] = val & 0xff;
	buf[2] = (val >> 8) & 0xff;
	buf[1] = (val >> 16) & 0xff;
	buf[0] = (val >> 24) & 0xff;
}

/* map enumerated pin number to pin register */
uint32_t pinreg[24] = {
	/* J1 P12 pin1-pin7 */
	CYREG_PRT12_PC0,
	CYREG_PRT12_PC1,
	CYREG_PRT12_PC2,
	CYREG_PRT12_PC3,
	CYREG_PRT12_PC4,
	CYREG_PRT12_PC5,
	CYREG_PRT12_PC6,
	CYREG_PRT12_PC7,
	/* J2 P3 pin8-pin16 */
	CYREG_PRT3_PC0,
	CYREG_PRT3_PC1,
	CYREG_PRT3_PC2,
	CYREG_PRT3_PC3,
	CYREG_PRT3_PC4,
	CYREG_PRT3_PC5,
	CYREG_PRT3_PC6,
	CYREG_PRT3_PC7,
	/* J3 P0 pin17-pin 24 */
	CYREG_PRT0_PC0,
	CYREG_PRT0_PC1,
	CYREG_PRT0_PC2,
	CYREG_PRT0_PC3,
	CYREG_PRT0_PC4,
	CYREG_PRT0_PC5,
	CYREG_PRT0_PC6,
	CYREG_PRT0_PC7,
};

/* Configure pin drive mode
 * @param val - array of 24 8bit values
 *   PIN_DM_ALG_HIZ        Analog HiZ
 *   PIN_DM_DIG_HIZ        Digital HiZ
 *   PIN_DM_RES_UP        Resistive pull up
 *   PIN_DM_RES_DWN        Resistive pull down
 *   PIN_DM_OD_LO        Open drain - drive low
 *   PIN_DM_OD_HI        Open drain - drive high
 *   PIN_DM_STRONG        Strong CMOS Output
 *   PIN_DM_RES_UPDWN    Resistive pull up/down
 */
void set_drv_mode(int pin, enum drive_mode mode) {
	switch(mode) {
	case DM_PU:
		CyPins_SetPinDriveMode(pinreg[pin], PIN_DM_RES_UP);
		break;
	case DM_PD:
		CyPins_SetPinDriveMode(pinreg[pin], PIN_DM_RES_DWN);
		break;
	case DM_OD:
		CyPins_SetPinDriveMode(pinreg[pin], PIN_DM_OD_LO);
		break;
	case DM_TP:
		CyPins_SetPinDriveMode(pinreg[pin], PIN_DM_STRONG);
		break;
	}
}

/* Configure pin direction
 * @param val - 32bit value cooresponding to each pin
 *              1 = input, 0 = output  
 */
void set_gpio_dir(uint32 val) {
	int i;

	for (i = 0; i < 24; i++) {
		/* input */
		if (val & (1<<i)) {
			/* needs to be HiZ digital */
			CyPins_SetPinDriveMode(pinreg[i], PIN_DM_DIG_HIZ);
		}

		/* output selectable */ 
		else {
			set_drv_mode(i, cfg.drivemode[i]);
		}
	}
	cfg.direction = val;
}

/* Configure output state
 * @param val - 32bit value cooresponding to each pin
 *              1 = high, 0 = low
 * input pins are not affected
 */
void set_gpio(uint32 val) {
	/* Outputs are driven from the CPU by writing to the port data reg */
	CY_SET_REG8(CYREG_PRT12_DR, val & 0xff);
	CY_SET_REG8(CYREG_PRT3_DR, (val >> 8) & 0xff);
	CY_SET_REG8(CYREG_PRT0_DR, (val >> 16) & 0xff);

	LED1_Write((val & (1<<30)) ? 1 : 0);
	LED2_Write((val & (1<<31)) ? 1 : 0);
}

uint32_t get_gpio_dir(void)
{
	return cfg.direction;
}

uint32_t get_gpio(void)
{
	/* get state of the pin from the Port Status register (PS) */
	uint32_t val = CY_GET_REG8(CYREG_PRT12_PS) |
		       (CY_GET_REG8(CYREG_PRT3_PS) << 8) |
		       (CY_GET_REG8(CYREG_PRT0_PS) << 16);

	if (LED1_Read())  val |= 1<<30;
	if (LED2_Read())  val |= 1<<31;

	return val;
}

static void default_config() {
	int i;

	memset(&cfg, 0, sizeof(cfg));
	cfg.size = sizeof(cfg);
	cfg.version = 1;
	memcpy(cfg.serial, "unknown", 7); /* bogus serial */

	/* wait in bootloader if (Ports & blmask) == blmagic */
	cfg.blmode = BLMODE_WAIT_IF_MAGIC; /* wait per mask*/
	cfg.blmask = 0x000000ff; /* J1/P12 */
	cfg.blmagic = 0x00000055;

	/* all GPIO inputs */
	cfg.direction = 0x00ffffff;
	/* drive mode strong */
	for (i = 0; i < 24; i++)
		cfg.drivemode[i] = DM_TP;
}

static void save_config() {
	int i, chksm = 0;
	uint8 *buf = (uint8 *)&cfg;

	/* calculate checksum */
	for (i = 4; i < sizeof(cfg); i++)
		chksm += buf[i];
	cfg.chksum = chksm;

	EEPROM_Start();
	/* it is necessary to acquire the die temperature before EEPROM write */
	CySetTemp();
	for (i = 0; i < (sizeof(cfg) / CYDEV_EEPROM_ROW_SIZE) + 1; ++i)
		EEPROM_Write(((uint8*)&cfg) + (i * CYDEV_EEPROM_ROW_SIZE), i);
	EEPROM_Stop();
}

static void load_config() {
	uint8* eeprom = (uint8*)CYDEV_EE_BASE;
	uint8* buf = (uint8*) &cfg;
	int i, z = -1, chksm = 0;

	/* read config to RAM then turn off EEPROM (to save power) */
	EEPROM_Start();
	CyDelayUs(5); /* per datasheet */
	memcpy(&cfg, eeprom, sizeof(cfg));
	EEPROM_Stop(); /* turn off EEPROM to conserve power */

	/* validate data */
	for (i = 0; i < sizeof(cfg); i++) {
		if (i > 3)
			chksm += buf[i];
		if (buf[i])
			z = i; /* first non-zero byte */
	}

	/* default configuration for un-configured/corrupt EEPROM */
	if ((z == -1) /* all zeros */
	 || (cfg.chksum != chksm) /* checksum failed */
	) {
		default_config();
		save_config();
	}

	/* Set serial string */
	for (i = 0; i < sizeof(cfg.serial); i++)
		serialnum[2 + (i*2)] = cfg.serial[i];
	serialnum[0] = 2 + sizeof(cfg.serial)*2; /* DescriptorLen */
	serialnum[1] = 0x3u; /* DescriptorType: STRING */
	USBFS_SerialNumString(serialnum);
}

static int init_peripherals(void)
{
	load_config();

	/* GPIO */
	set_gpio_dir(cfg.direction);

	return 0;
}

/* Process OUT data (data sent from the PC host) */
/* Receive data packet from PC */
void ProcessInput(uint8 *in)
{
	uint32 val = get_uint32(in+4);
	switch(in[0]) {
	case BOOTLOADER:
		/* jump to bootloader */
		Bootloadable_Load();
		break;
	case SET_CONFIG: {
		char cmd = in[4];
		char save = 1;

		val = get_uint32(in+5);
		switch(cmd) {
		case 0:
			memcpy(cfg.serial, in+5, sizeof(cfg.serial));
			break;
		case 1:
			cfg.blmode = val & 0xff;
			break;
		case 2:
			cfg.blmask = val;
			break;
		case 3:
			cfg.blmagic = val;
			break;
		case 4:
			cfg.direction = val;
			break;
		case 255:
			default_config();
			save_config();
			break;
		default:
			if (cmd > 4 && cmd < 29)
				cfg.drivemode[cmd - 5] = val;
			else
				save = 0;
			break;
		}
		if (save)
			save_config();
	}	break;

	/* GPIO */
	case SET_GPIO_DIR:
		set_gpio_dir(val);
		break;
	case SET_GPIO:
		set_gpio(val);
		break;
	}
}

/* Process EP1 - Process the IN data (data sent to the PC host) */
/* Transmit data packet to PC */
void PrepareOutput(uint8 *out, uint8 *in)
{
	uint8 cmd = in[0];

	memset(out, 0, 64);
	out[0] = cmd;
	switch(cmd) {
	case GET_CONFIG:
	case SET_CONFIG:
		cmd = in[4];
		out[4] = cmd;
		switch(cmd) {
		case 0:
			memcpy(out+8, cfg.serial, sizeof(cfg.serial));
			break;
		case 1:
			set_uint32(out+8, cfg.blmode);
			break;
		case 2:
			set_uint32(out+8, cfg.blmask);
			break;
		case 3:
			set_uint32(out+8, cfg.blmagic);
			break;
		case 4:
			set_uint32(out+8, cfg.direction);
			break;
		default:
			if (cmd > 4 && cmd < 29)
				set_uint32(out+8, cfg.drivemode[cmd - 5]);
			else
				memcpy(out+8, &cfg, sizeof(cfg));
			break;
		}
		break;

	/* GPIO */
	case GET_GPIO_DIR:
	case SET_GPIO_DIR:
		set_uint32(out+0x04, get_gpio_dir());
		break;
	case SET_GPIO:
	case GET_GPIO:
		set_uint32(out+0x04, get_gpio());
		break;
	}
}

void dataPoll()
{
	int reset = 0;

	if (!usbReady || USBFS_IsConfigurationChanged()) {
		reset = 1;
	}
	usbReady = USBFS_bGetConfiguration();

	if (!usbReady)
		return;

	if (reset) {
		USBFS_EnableOutEP(USB_EP_OUT);
		usbInEpState = USB_IDLE;
	}

	/* check for packet */
	if (USBFS_GetEPState(USB_EP_OUT) == USBFS_OUT_BUFFER_FULL) {
		/* The host sent data for us to process */
		int byteCount = USBFS_GetEPCount(USB_EP_OUT);

		/* Read the OUT endpoint and store data
		   in OUT_Data_Buffer */
		USBFS_ReadOutEP(USB_EP_OUT, OUT_Data_Buffer, byteCount);

		/* Process the data received from the host */
		ProcessInput(OUT_Data_Buffer);

		/* Prepare response */
		PrepareOutput(IN_Data_Buffer, OUT_Data_Buffer);

		/* Enable IN transfer */
		USBFS_LoadInEP(USB_EP_IN, IN_Data_Buffer, sizeof(IN_Data_Buffer));

		/* wait for it to be read */
		if (USBFS_GetEPState(USB_EP_IN) != USBFS_IN_BUFFER_EMPTY) {
			CyDelay(1);
		}

		USBFS_LoadEP(USB_EP_IN, IN_Data_Buffer, sizeof(IN_Data_Buffer));
		/* Enable the OUT EP to receive data */
		USBFS_EnableOutEP(USB_EP_OUT);
	}
}

void main()
{
	/* Enable Global Interrupts */
	CyGlobalIntEnable;

	/* Initialize Peripherals */
	init_peripherals();

	/* Start USBFS Operation (voltage depending on DWR config) */
	USBFS_Start(0, USBFS_DWR_VDDD_OPERATION);
	usbInEpState = USB_IDLE;
	usbReady = 0;

	/* Wait for Device to be assigned an address */
	while(USBFS_deviceAddress == 0) {
		/* Waiting for device address to be assigned */
	}

	while (1) {
		dataPoll();
	};
}
