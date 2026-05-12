#include "Point.h"

Point::Point() : distance(0), angle(0), grade(0) {}

Point::Point(uint8_t pointData[], uint8_t nPointsOnScan, uint16_t scanSA, uint16_t scanEA) {
    /*
    * PointData[] is the part of the LiDAR frame that holds the data of a single point.
    * The point data consists of 2 bytes for distance (little-endian) and then a byte
    * for reading grade.
    * This constructor is used to fill the scanPoints[] of an object Scan instanced from
    * a full LiDAR frame.
    */
    this->grade = pointData[2];                             // Set the grade
    this->distance = (pointData[1] << 8) | pointData[0];    // Set the distance (uint16_t)
    
    /*
    * The span was calculated in int32_t to allow negative numbers in the case of a single
    * scan starting close to the 360 deg mark and going beyond 360 deg.
    */
    int32_t span = (int32_t)scanEA - (int32_t)scanSA;

    if (span < 0) span += 36000; // Case span is negative, give it a full spin to same position.

    /*
    * Indivdual point angle is exrtapolated from the begining and end of a scan, assuming
    * an equal distance between each point.
    */
    uint16_t angleIncrement = span / 11;
    uint16_t finalAngle = scanSA + (nPointsOnScan * angleIncrement);

    // Give point angle a full spin backwards into same position in case it goes beyond 360 deg
    if (finalAngle >= 36000) finalAngle -= 36000;
    this->angle = finalAngle;
}

uint16_t Point::getDistance() const{ return this->distance; }

uint16_t Point::getAngle() const{ return this->angle; }

uint8_t Point::getGrade() const{ return this->grade; }