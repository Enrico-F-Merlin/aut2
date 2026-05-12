#ifndef SCAN_H
#define SCAN_H

#include <Arduino.h>
#include "config.h"
#include "Point.h"

class Scan{
    private:
        uint16_t frequecy;
        uint16_t startAngle;
        uint16_t endAngle;
        uint16_t timeStamp;
        uint8_t CRC8;

        Point scanPoints[12];

    public:
        Scan();

        Scan(uint8_t lidarData[]);

        uint16_t getStartAngle() const;

        uint16_t getEndAngle() const;

        Point getPoint(int index) const;
};

#endif