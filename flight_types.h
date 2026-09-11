#pragma once

struct Flight {
  char callsign[16];
  char aircraftType[16];
  char origin[5];
  char destination[5];
  int altitude;
  int speed;
  int heading;
  int verticalRate;
  float distance;       // nm from home
  float lat;
  float lon;
  float originLat, originLon;
  float destLat, destLon;
  bool hasOriginCoords;
  bool hasDestCoords;
  int remMin;           // estimated minutes to dest (-1 unknown)
  int durMin;           // estimated total block time (-1 unknown)
  bool isInternational;
};

struct RawFlight {
  char  callsign[16];
  char  aircraftType[16];
  float lat, lon;
  int   altitude, speed, heading, vertRate;
  bool  fromWatchLookup;
};

struct RouteCache {
  char callsign[16];
  char origin[5];
  char destination[5];
  float originLat, originLon, destLat, destLon;
  bool hasOriginCoords, hasDestCoords;
  unsigned long timestamp;
  bool valid;
  bool isInternational;
};
