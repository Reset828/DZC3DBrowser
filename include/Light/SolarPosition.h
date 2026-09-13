#ifndef __SOLAR_POSITION_H__
#define __SOLAR_POSITION_H__

// 当地真太阳时下的太阳位置。
// 12:00 = 太阳在当地子午圈上（北半球为正南）。不使用经度、时区或均时差。
// 模型坐标：+X 东，+Y 北，+Z 上。direction 指向太阳。

struct SolarPosition {
    float altitudeRadians;
    float azimuthFromNorthRadians;
    float directionX;
    float directionY;
    float directionZ;
    bool aboveHorizon;
};

struct SolarPositionQuery {
    float latitudeDegrees;
    int year;
    int month;
    int day;
    float trueSolarTimeHours;
};

// 计算太阳位置。
SolarPosition ComputeSolarPosition(const SolarPositionQuery& query);

#endif //__SOLAR_POSITION_H__
