#ifndef TM_DEFINES_H
#define TM_DEFINES_H

/* Put your global defines for all libraries here used in your project */

/* Use SPI communication with SDCard */
#define	FATFS_USE_SDIO				0


#define FATFS_SPI				SPI2
#define FATFS_SPI_PINSPACK		TM_SPI_PinsPack_2

/* Custom CS pin for SPI communication */	
#define FATFS_CS_PORT		GPIOB
#define FATFS_CS_PIN		GPIO_Pin_5


#endif