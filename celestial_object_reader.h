#ifndef CELESTIAL_OBJECT_READER_H
#define CELESTIAL_OBJECT_READER_H

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "celestial_objects.h"

class CelestialObjectReader
{
public:
    static std::vector<CelestialObject> ReadFromCSV(const std::string& filepath)
    {
        std::vector<CelestialObject> objects;
        std::ifstream file(filepath);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + filepath);
        }

        std::string line;
        bool firstLine = true;
        while (std::getline(file, line))
        {
            if (firstLine)
            {
                firstLine = false;
                continue;
            }

            std::stringstream ss(line);
            std::string name;
            std::string ra_h, ra_m, ra_s;
            std::string dec_sign, dec_d, dec_m, dec_s;
            std::string magnitude;
            std::string bayer;

            if (!std::getline(ss, name, ',') ||
                !std::getline(ss, ra_h, ',') ||
                !std::getline(ss, ra_m, ',') ||
                !std::getline(ss, ra_s, ',') ||
                !std::getline(ss, dec_sign, ',') ||
                !std::getline(ss, dec_d, ',') ||
                !std::getline(ss, dec_m, ',') ||
                !std::getline(ss, dec_s, ',') ||
                !std::getline(ss, magnitude, ','))
            {
                continue;
            }
            // Bayer designation is optional (empty for clusters/nebulae);
            // the trailing comma may yield an empty string if no value.
            std::getline(ss, bayer);

            double raHours = std::stod(ra_h) + std::stod(ra_m) / 60.0 + std::stod(ra_s) / 3600.0;
            double decDegrees = std::stod(dec_sign) * (std::stod(dec_d) + std::stod(dec_m) / 60.0 + std::stod(dec_s) / 3600.0);
            double mag = std::stod(magnitude);

            objects.emplace_back(name, raHours, decDegrees, mag, bayer);
        }

        return objects;
    }
};

#endif
