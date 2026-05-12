#ifndef CONFIG_H
#define CONFIG_H

// ultrassonic pins
#define trigPin 13
#define echoPin 12

// UART config
#define UART_NUM UART_NUM_2
#define LIDAR_RX_PIN 18
#define BAUD_RATE 230400

// LiDAR incoming frame info (LiDAR model specific)
#define FROM_LIDAR_FRAME_SIZE 47
#define HEADER_1 0x54
#define HEADER_2 0x2C

// Point validation logic config
#define LIDAR_VALID_CONE_ANGLE 18000    // Scans will be considered valid if in range 0 - 120.00 deg
#define NUMBER_OF_SECTORS 360           // Number of divisions in valid cone angle. For CAN data reduction
#define MAX_LIDAR_DIST 12000            // This one is greter than the raspi's in case we want to increase
/*
* This (SCANS_ON_CONE) is for memory allocation on the Cone scanCollection.
* Number of scans vary based on other parameters.
* ex: 12 or 13 scans for 120 deg cone with 60 sectors
* Will be removed after vector introduction.
*/
#define SCANS_ON_CONE 16 

// CAN config
#define CAN_TX_PIN 15
#define CAN_RX_PIN 16

// The followin 3 will be concatenated into the complete ID
#define CAN_LIDAR_PRIORITY 0    // LiDAR message priority at CAN bus (0 is highest)
#define CAN_LIDAR_ID 2          // LiDAR mesage id at CAN bus
#define backZ 2                 // LiDAR position at bike

// The followin 3 will be concatenated into the complete ID
#define CAN_Ultrass_PRIORITY 1    // Ultrass message priority at CAN bus (0 is highest)
#define CAN_Ultrass_ID 3          // Ultrass mesage id at CAN bus
//#define backZ 2                 // Ultrass position at bike

/*
* The following headers and footer are arbitrary. Footer set as 0xFE
* as 0xFF was defined as the "fill" for non valid distance at sectors
*/
#define CAN_HEADER_1 0xAA
#define CAN_HEADER_2 0x55
#define CAN_FOOTER 0xFE

#endif