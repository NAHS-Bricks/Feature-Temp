#ifndef NAHS_BRICKS_FEATURE_TEMP_H
#define NAHS_BRICKS_FEATURE_TEMP_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <nahs-Bricks-Lib-HDC1080.h>
#include <nahs-Bricks-Lib-SHT4x.h>
#include <nahs-Bricks-Feature-BaseClass.h>
#include <nahs-Bricks-Lib-RTCmem.h>
#include <nahs-Bricks-Lib-FSmem.h>

class NahsBricksFeatureTemp : public NahsBricksFeatureBaseClass {
    private:  // Variables
        static const uint16_t version = 1;
        static const uint8_t MAX_TEMP_SENSORS_COUNT = 8;
        bool _HDC1080_connected = false;
        bool _SHT4x_connected = false;
        typedef struct {
            bool precisionRequested;
            bool sensorCorrRequested;
            uint8_t sensorPrecision;
            uint8_t sensorCount;  // Holds number of currently connected temp-sensors
            HDC1080_SerialNumber HDC1080SN;  // holds SN of HDC1080 Sensor if connected
            float HDC1080Corr;  // holds correction value for HDC1080 if connected
        } _RTCdata;
        typedef struct {
            float sensorCorr[MAX_TEMP_SENSORS_COUNT / 2];  // holds currently used sensor correction values
        } _SCdata;
        typedef struct {
            DeviceAddress sensorAddr[4];  // holds addresses of currently connected sensors
        } _SAdata;
        typedef struct {
            SHT4x_SerialNumber SHT4xSN;  // holds SN of SHT4x Sensor if connected
            float SHT4xCorr;  // holds correction value for SHT4x if connected
        } _SHTdata;
        _SCdata* SCdata = RTCmem.registerData<_SCdata>();
        _SAdata* SAdata1 = RTCmem.registerData<_SAdata>();
        _SAdata* SAdata2 = RTCmem.registerData<_SAdata>();
        _RTCdata* RTCdata = RTCmem.registerData<_RTCdata>();
        _SHTdata* SHTdata = RTCmem.registerData<_SHTdata>();
        JsonObject FSdata = FSmem.registerData("t");
        uint8_t _oneWirePin;
        OneWire _oneWire;
        DallasTemperature _DS18B20;

    public: // BaseClass implementations
        NahsBricksFeatureTemp();
        String getName();
        uint16_t getVersion();
        void begin();
        void start();
        void deliver(JsonDocument* out_json);
        void feedback(JsonDocument* in_json);
        void end();
        void printRTCdata();
        void printFSdata();
        void brickSetupHandover();

    public:  // Brick-Specific setter
        void setSensorsPin(uint8_t pin);

    private:  // internal Helpers
        String _deviceAddrToString(uint8_t sensor_index);
        String _deviceAddrToString(DeviceAddress deviceAddress);
        float _getTempC(uint8_t sensor_index);
        void _transmitPrecisionToSensors(bool inBackground);

    private:  // BrickSetup Helpers
        void _printMenu();
        void _identifySensor();
        void _readSensorsRaw();
        void _readSensorsCorr();
        void _setDefaultPrecision();
        void _setDefaultCorr();
        void _deleteDefaultCorr();
};

#if !defined(NO_GLOBAL_INSTANCES)
extern NahsBricksFeatureTemp FeatureTemp;
#endif

#endif // NAHS_BRICKS_FEATURE_TEMP_H
