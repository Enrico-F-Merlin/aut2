#ifndef POINT_H
#define POINT_H

#include <Arduino.h>

class Point {
    private:
        uint16_t distance;
        uint16_t angle;
        uint8_t grade;

    public:
        Point();
        
        Point(uint8_t pointData[], uint8_t nPointsOnScan, uint16_t scanSA, uint16_t scanEA);

        uint16_t getDistance() const;

        uint16_t getAngle() const;

        uint8_t getGrade() const;
};

#endif