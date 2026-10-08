#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "wmm_mag.h"

class MagneticDeclination {
public:
    bool initialize(const char* cof_path = "WMM2025.COF") {
        return wmm_init(cof_path) != 0;
    }

    double true_north(double magnetic_bearing, double lat, double lon, double alt_km, double decimal_year) {
        double decl = 0.0, incl = 0.0, F = 0.0, GV = 0.0;
        double X = 0.0, Y = 0.0, Z = 0.0, H = 0.0;

        int warning = wmm_point(alt_km, lat, lon, decimal_year,
                                &decl, &incl, &F, &GV, &X, &Y, &Z, &H);

        if (warning != 0 || std::isnan(decl)) {
            fprintf(stderr, "Warning: weak horizontal field or invalid declination at this location\n");
            return NAN;
        }

        return magnetic_bearing + decl;
    }

    bool version(double* epoch, char* model_name, size_t name_cap,
                 char* release_date, size_t date_cap) {
        wmm_version(epoch, model_name, name_cap, release_date, date_cap);
        return epoch != nullptr && *epoch > 0.0;
    }
};

static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s <lat> <lon> [magnetic_bearing]\n", prog);
    fprintf(stderr, "  lat: latitude in decimal degrees (-90 to 90)\n");
    fprintf(stderr, "  lon: longitude in decimal degrees (-180 to 180)\n");
    fprintf(stderr, "  magnetic_bearing: optional bearing in degrees (default: 0.0)\n");
}

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        usage(argv[0]);
        return 1;
    }

    double lat = atof(argv[1]);
    double lon = atof(argv[2]);
    double magnetic_bearing = (argc == 4) ? atof(argv[3]) : 0.0;

    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        fprintf(stderr, "Error: lat must be -90..90, lon must be -180..180\n");
        return 1;
    }

    MagneticDeclination mag;

    if (!mag.initialize("WMM2025.COF")) {
        fprintf(stderr, "Failed to initialize WMM2025 model\n");
        return 1;
    }

    double epoch = 0.0;
    char name[32] = {0};
    char date[16] = {0};
    mag.version(&epoch, name, sizeof(name), date, sizeof(date));
    printf("Model: %s (epoch %.1f, released %s)\n\n", name, epoch, date);

    double alt = 0.0;
    double year = 2025.0 + (8.0 + 31.0/60.0) / 365.0;

    double true_bearing = mag.true_north(magnetic_bearing, lat, lon, alt, year);

    if (!std::isnan(true_bearing)) {
        printf("Location: %.4f N, %.4f E\n", lat, lon);
        printf("Magnetic bearing: %.2f deg\n", magnetic_bearing);
        printf("True bearing:     %.2f deg\n", true_bearing);
        printf("Declination:      %.2f deg (E-positive)\n", true_bearing - magnetic_bearing);
    }

    return 0;
}
