/*********************************************************************
* main.cpp                                                           *
*                                                                    *
* This is the main file for the firmware for the ESP32-S3-DevkitC-1U *
* that will be supplying power and recieving data from the LiDAR at  *
* the back of the bike.                                              *
*                                                                    *
**********************************************************************/

#include <Arduino.h>
#include <ESP32-TWAI-CAN.hpp>
#include "driver/uart.h"
#include "Cone.h"

int frameIndex = 0;                             // Frame index for incoming LiDAR frame bytes tracking
uint8_t lidarData[FROM_LIDAR_FRAME_SIZE];       // Array that will hold incoming bytes. Logic start when full
Cone cone(0, LIDAR_VALID_CONE_ANGLE);

TaskHandle_t ultrassonic_handle = NULL;

void IRAM_ATTR lidar_task(void *pvParameters);  // Task to be pinned into one of ESP's cores
void IRAM_ATTR ultrassonic_task(void *pvParameters);


void setup() {
  Serial.begin(BAUD_RATE);    // Baud rate changed through config.h
  delay(2000);

  ESP32Can.setTxQueueSize(64); // to avoid filling and discarding messages

  // Start and verify if CAN was initialized correctly
  if (ESP32Can.begin(TWAI_SPEED_500KBPS, CAN_TX_PIN, CAN_RX_PIN)) {
    Serial.println("CAN Bus Initialized");
  } else {
    Serial.println("CAN Bus Failed to Initialize");
  }

  // UART configuration
  uart_config_t uart_config = {
    .baud_rate = BAUD_RATE,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 122,
  };

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(trigPin, LOW);

  uart_driver_install(UART_NUM, 4096, 0, 0, NULL, 0);
  uart_param_config(UART_NUM, &uart_config);
  uart_set_pin(UART_NUM, -1, LIDAR_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE); //PINS at config.h

  xTaskCreatePinnedToCore(lidar_task, "LIDAR_TASK", 8192, NULL, 2, NULL, 1); //LiDAR task pinned
  xTaskCreatePinnedToCore(ultrassonic_task, "ULTRASSONIC_TASK", 4096, NULL, 1, &ultrassonic_handle, 0);
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}

void IRAM_ATTR lidar_task(void *pvParameters) {
  uint8_t incomingByte;   // Frame Byte buffer
  uint8_t sentCounter = 0;

  while(true) {
    /*
    * Keeps trying to fill the LiDAR frame buffer array. For syncronization, only 
    * proceeds when it is verified that the two first bytes in the array are the
    * header bytes (indicated at config.h, specific from LiDAR model)
    */
    if (uart_read_bytes(UART_NUM, &incomingByte, 1, portMAX_DELAY) > 0) {
      if (frameIndex == 0 && incomingByte != HEADER_1) continue;
      if (frameIndex == 1 && incomingByte != HEADER_2) {
        frameIndex = 0;
        continue;
      }
      lidarData[frameIndex++] = incomingByte;

      // Process frame when it is full
      if (frameIndex == FROM_LIDAR_FRAME_SIZE) {
        Scan scan(lidarData);

        /*
        * Cone is sent only it has recieving all sequential valid scans that
        * will constitute the full cone.
        */ 
        if (!cone.growCone(scan) && cone.getNScans() != 0) {
          cone.sendConeFan();
          sentCounter++;

          if (sentCounter >= 2) {
            xTaskNotifyGive(ultrassonic_handle);
            sentCounter = 0;
          }

          cone = Cone(0, LIDAR_VALID_CONE_ANGLE); // Reset buffer cone
        }

        frameIndex = 0;   // "Empties" the frame buffer to recieve next scan
      }
    }
  }
}



void IRAM_ATTR ultrassonic_task(void *pvParameters) {

  uint32_t UltrassID = (uint32_t)(CAN_Ultrass_PRIORITY << 26 | CAN_Ultrass_ID << 8 | backZ);

  // Configure CAN message
  CanFrame canFrame = { 0 };              // Reset message to avoid overlap
  canFrame.identifier = UltrassID;        // Set ID on CAN bus
  canFrame.extd = 1;                      // Set extended ID format
  canFrame.data_length_code = 4;          // float lenght

  float pool[5] = {0,0,0,0,0};
  uint8_t poolIdx = 0;

  long duration = 0;
  float distance = 0; //cm

  float sum = 0.0f;
  uint8_t n = 0;

  float avg_dist = 0;

  while (true) {
    
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(25);
    digitalWrite(trigPin, LOW);

    duration = pulseIn(echoPin, HIGH, 30000);

    distance = duration * 0.0343 / 2;

    if (distance > 500.0f) {
      distance = 0.0f;
    }

    pool[poolIdx] = distance;
    poolIdx = (poolIdx + 1) % 5;

    sum = 0.0f;
    n = 0;
    for (uint8_t i = 0; i < 5; i++) {
      if (pool[i] == 0) continue;
      sum += pool[i];
      n++;
    }

    if (n != 0) {
      avg_dist = sum / n;
    }

    if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
      
      memcpy(canFrame.data, &avg_dist, sizeof(avg_dist));

      ESP32Can.writeFrame(canFrame);
    }

    vTaskDelay(pdMS_TO_TICKS(50)); 
  }

}
