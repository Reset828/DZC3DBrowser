#include "SolarPosition.h"
#include <cmath>
#include <algorithm>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;

// 判断是否闰年。
bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// 返回指定年月的天数。
int DaysInMonth(int year, int month) {
    static const int kDays[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && IsLeapYear(year)) {
        return 29;
    }
    return kDays[month];
}

// 计算年内日序。
int DayOfYear(int year, int month, int day) {
    month = std::clamp(month, 1, 12);
    day = std::clamp(day, 1, DaysInMonth(year, month));

    static const int kDaysBeforeMonth[] = {
        0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    int n = kDaysBeforeMonth[month] + day;
    if (month > 2 && IsLeapYear(year)) {
        ++n;
    }
    return n;
}

// 将小时数限制在一天范围内。
float WrapHours(float hours) {
    hours = std::fmod(hours, 24.0f);
    if (hours < 0.0f) {
        hours += 24.0f;
    }
    return hours;
}

// 将角度限制在 2π 范围内。
float WrapTwoPi(float radians) {
    radians = std::fmod(radians, kTwoPi);
    if (radians < 0.0f) {
        radians += kTwoPi;
    }
    return radians;
}

} // namespace

// 计算太阳位置。
SolarPosition ComputeSolarPosition(const SolarPositionQuery& query) {
    const float latitudeDeg = std::clamp(query.latitudeDegrees, -90.0f, 90.0f);
    const float latitude = latitudeDeg * (kPi / 180.0f);
    const float hours = WrapHours(query.trueSolarTimeHours);
    const int dayOfYear = DayOfYear(query.year, query.month, query.day);

    // Spencer 1971 赤纬。γ 含真太阳时的日内小数，避免日期边界处跳变。
    const float yearLength = IsLeapYear(query.year) ? 366.0f : 365.0f;
    const float gamma = kTwoPi * ((static_cast<float>(dayOfYear) - 1.0f)
        + (hours - 12.0f) / 24.0f) / yearLength;
    const float declination =
        0.006918f
        - 0.399912f * std::cos(gamma) + 0.070257f * std::sin(gamma)
        - 0.006758f * std::cos(2.0f * gamma) + 0.000907f * std::sin(2.0f * gamma)
        - 0.002697f * std::cos(3.0f * gamma) + 0.001480f * std::sin(3.0f * gamma);

    const float hourAngle = (hours - 12.0f) * (kPi / 12.0f);
    const float sinAltitude =
        std::sin(latitude) * std::sin(declination)
        + std::cos(latitude) * std::cos(declination) * std::cos(hourAngle);
    const float altitude = std::asin(std::clamp(sinAltitude, -1.0f, 1.0f));
    const float cosAltitude = std::cos(altitude);

    SolarPosition result{};
    result.altitudeRadians = altitude;
    result.aboveHorizon = sinAltitude > 0.0f;

    if (cosAltitude < 1.0e-6f) {
        result.azimuthFromNorthRadians = 0.0f;
        result.directionX = 0.0f;
        result.directionY = 0.0f;
        result.directionZ = (sinAltitude >= 0.0f) ? 1.0f : -1.0f;
        return result;
    }

    const float sinAzimuth = -(std::cos(declination) * std::sin(hourAngle)) / cosAltitude;
    const float cosAzimuth =
        (std::sin(declination) * std::cos(latitude)
            - std::cos(declination) * std::sin(latitude) * std::cos(hourAngle)) / cosAltitude;
    const float azimuth = WrapTwoPi(std::atan2(sinAzimuth, cosAzimuth));

    result.azimuthFromNorthRadians = azimuth;
    result.directionX = cosAltitude * std::sin(azimuth);
    result.directionY = cosAltitude * std::cos(azimuth);
    result.directionZ = std::sin(altitude);
    return result;
}
