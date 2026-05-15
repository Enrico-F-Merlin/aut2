/*********************************************************************
* main.cpp                                                           *
*                                                                    *
* This is the main file for the firmware for the ESP32-S3-DevkitC-1U *
* that will be driving the RFID reader and sendind the UID reads     *
* through CAN into the RasPI                                         *
*                                                                    *
**********************************************************************/

#include <SPI.h>
#include <MFRC522.h>
#include <ESP32-TWAI-CAN.hpp>

// --- RFID Pins ---
#define SS_PIN  10  // SDA
#define RST_PIN 9   // Reset

// --- CAN Bus Pins ---
#define CAN_TX_PIN 4
#define CAN_RX_PIN 5

// RFID CAN frame ID
#define RFID_ID 0x030

MFRC522 mfrc522(SS_PIN, RST_PIN);

CanFrame canFrame = { 0 }; // Frame is configured on setup()

void setup() {

  //Serial.begin(9600);
  SPI.begin();
  
  mfrc522.PCD_Init();
  
  delay(5000); 
  
  // Prints reader details
  //mfrc522.PCD_DumpVersionToSerial();
  
  // Here we could send CAN error frames, but it is most likely overkill for the project
  if (ESP32Can.begin(TWAI_SPEED_500KBPS, CAN_TX_PIN, CAN_RX_PIN)) {
    //Serial.println("CAN Bus Initialized");
  } else {
    //Serial.println("CAN Bus Failed to Initialize");
  }

  canFrame.identifier = RFID_ID;  // Frame ID is always same
  canFrame.extd = 0;              // 0 = Standard 11-bit ID format
  
  //Serial.println(F("Aproxime o seu cartao/tag do leitor..."));
}

void loop() {

  if ( ! mfrc522.PICC_IsNewCardPresent()) return; // Checks for a card close by

  if ( ! mfrc522.PICC_ReadCardSerial()) return;   // Read the UID from detected card

  // Shows the UID on the Serial Monitor
  //Serial.print(F("UID da Tag:"));
  //for (byte i = 0; i < mfrc522.uid.size; i++) {
  //  Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
  //  Serial.print(mfrc522.uid.uidByte[i], HEX);
  //}
  //Serial.println();
  
  // Most tags have 4 bytes, but some have 7. We cap it at 8 (max CAN payload).
  byte uidLength = mfrc522.uid.size;
  if (uidLength > 8) uidLength = 8;
  
  // Set the data length code dynamically based on the tag
  canFrame.data_length_code = uidLength;

  // Copy the UID bytes into the CAN frame data
  for (byte i = 0; i < uidLength; i++) {
    canFrame.data[i] = mfrc522.uid.uidByte[i];
  }

  // Send the frame over the CAN bus
  if (ESP32Can.writeFrame(canFrame)) {
    //Serial.println(F("CAN Frame sent successfully!"));
  } else {
    //Serial.println(F("Error sending CAN Frame."));
  }

  // Halts reading so we don't spam the same card in milliseconds
  mfrc522.PICC_HaltA();
}