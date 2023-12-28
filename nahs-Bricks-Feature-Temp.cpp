#include <nahs-Bricks-Feature-Temp.h>
#include <nahs-Bricks-Lib-SerHelp.h>

NahsBricksFeatureTemp::NahsBricksFeatureTemp() {
}

/*
Returns name of feature
*/
String NahsBricksFeatureTemp::getName() {
    return "temp";
}

/*
Returns version of feature
*/
uint16_t NahsBricksFeatureTemp::getVersion() {
    return version;
}

/*
Configures FSmem und RTCmem variables (prepares feature to be fully operational)
*/
void NahsBricksFeatureTemp::begin() {
    _oneWire.begin(_oneWirePin);
    _DS18B20.setOneWire(&_oneWire);
    _DS18B20.begin();

    _HDC1080_connected = HDC1080.begin();
    _SHT4x_connected = SHT4x.begin();

    if (!FSdata.containsKey("sCorr")) FSdata.createNestedObject("sCorr");  // dict with sensorAddr as key and sensorCorr as value
    if (!FSdata.containsKey("sPrec")) FSdata["sPrec"] = 11;  // default sensor precision

    if (!RTCmem.isValid()) {
        if (!_HDC1080_connected || !_SHT4x_connected) {
            delay(15);
            _HDC1080_connected = HDC1080.isConnected();
            _SHT4x_connected = SHT4x.isConnected();
        }
        RTCdata->precisionRequested = false;
        RTCdata->sensorCorrRequested = false;
        RTCdata->sensorPrecision = FSdata["sPrec"].as<uint8_t>();
        RTCdata->sensorCount = min(_DS18B20.getDeviceCount(), (uint8_t)MAX_TEMP_SENSORS_COUNT);

        for (uint8_t i = 0; i < MAX_TEMP_SENSORS_COUNT; i++) {
            if (i < RTCdata->sensorCount) {
                if (i < 4) _DS18B20.getAddress(SAdata1->sensorAddr[i], i);
                else _DS18B20.getAddress(SAdata2->sensorAddr[i], i);
                String addr = _deviceAddrToString(i);
                if (FSdata["sCorr"].as<JsonObject>().containsKey(addr)) {
                    SCdata->sensorCorr[i] = FSdata["sCorr"].as<JsonObject>()[addr].as<float>();
                }
                else SCdata->sensorCorr[i] = 0;
            }
            else {
                if (i < 4) memset(SAdata1->sensorAddr[i], 0, sizeof(SAdata1->sensorAddr[i]));
                else memset(SAdata2->sensorAddr[i], 0, sizeof(SAdata2->sensorAddr[i]));
                SCdata->sensorCorr[i] = 0;
            }
        }

        if (_HDC1080_connected) {
            HDC1080.getSN(RTCdata->HDC1080SN);
            String sn = HDC1080.snToString(RTCdata->HDC1080SN);
            if (FSdata["sCorr"].as<JsonObject>().containsKey(sn)) {
                RTCdata->HDC1080Corr = FSdata["sCorr"].as<JsonObject>()[sn].as<float>();
            }
            else RTCdata->HDC1080Corr = 0;
        }
        else {
            memset(RTCdata->HDC1080SN, 0, sizeof(RTCdata->HDC1080SN));
            RTCdata->HDC1080Corr = 0;
        }

        if(_SHT4x_connected) {
            SHT4x.getSN(SHTdata->SHT4xSN);
            String sn = SHT4x.snToString(SHTdata->SHT4xSN);
            if (FSdata["sCorr"].as<JsonObject>().containsKey(sn)) {
                SHTdata->SHT4xCorr = FSdata["sCorr"].as<JsonObject>()[sn].as<float>();
            }
            else SHTdata->SHT4xCorr = 0;
        }
        else {
            memset(SHTdata->SHT4xSN, 0, sizeof(SHTdata->SHT4xSN));
            SHTdata->SHT4xCorr = 0;
        }
    }
}

/*
Starts background processes like fetching data from other components
*/
void NahsBricksFeatureTemp::start() {
    // if brick just started up, configure the correct precision and do a dummy reading
    if(!RTCmem.isValid()) {
        _transmitPrecisionToSensors(false);
    }

    // Start the Temp-Conversion in Background as this takes some time
    _DS18B20.setWaitForConversion(false);
    _DS18B20.requestTemperatures();

    // Trigger Conversion of HDC1080 in Background if connected
    if (_HDC1080_connected) {
        HDC1080.triggerRead();
    }

    // Trigger Conversion of SHT4x in Background if connected
    if (_SHT4x_connected) {
        SHT4x.triggerRead();
    }
}

/*
Adds data to outgoing json, that is send to BrickServer
*/
void NahsBricksFeatureTemp::deliver(JsonDocument* out_json) {
    // deliver sensors precision if requested
    if (RTCdata->precisionRequested) {
        RTCdata->precisionRequested = false;
        out_json->operator[]("p").set(RTCdata->sensorPrecision);
    }

    // deliver sensors correction values if requested
    if (RTCdata->sensorCorrRequested) {
        RTCdata->sensorCorrRequested = false;

        JsonArray c_array;
        if (out_json->containsKey("c"))
            c_array = out_json->operator[]("c").as<JsonArray>();
        else
            c_array = out_json->createNestedArray("c");

        for (uint8_t i = 0; i < RTCdata->sensorCount; ++i) {
            JsonArray s_array = c_array.createNestedArray();
            s_array.add(_deviceAddrToString(i));
            s_array.add(SCdata->sensorCorr[i]);
        }
        if (_HDC1080_connected) {
            JsonArray s_array = c_array.createNestedArray();
            s_array.add(HDC1080.snToString(RTCdata->HDC1080SN));
            s_array.add(RTCdata->HDC1080Corr);
        }
        if (_SHT4x_connected) {
            JsonArray s_array = c_array.createNestedArray();
            s_array.add(SHT4x.snToString(SHTdata->SHT4xSN));
            s_array.add(SHTdata->SHT4xCorr);
        }
    }

    // wait for temperature conversion to complete and deliver the temperatures
    JsonArray t_array;
    if (out_json->containsKey("t"))
        t_array = out_json->operator[]("t").as<JsonArray>();
    else
        t_array = out_json->createNestedArray("t");

    while(!_DS18B20.isConversionComplete()) delay(1);
    for (uint8_t i = 0; i < RTCdata->sensorCount; ++i) {
        JsonArray s_array = t_array.createNestedArray();
        s_array.add(_deviceAddrToString(i));
        s_array.add(_getTempC(i) + SCdata->sensorCorr[i]);
    }
    if (_HDC1080_connected) {
        JsonArray s_array = t_array.createNestedArray();
        s_array.add(HDC1080.snToString(RTCdata->HDC1080SN));
        s_array.add(HDC1080.getT() + RTCdata->HDC1080Corr);
    }
    if (_SHT4x_connected) {
        JsonArray s_array = t_array.createNestedArray();
        s_array.add(SHT4x.snToString(SHTdata->SHT4xSN));
        s_array.add(SHT4x.getT() + SHTdata->SHT4xCorr);
    }
}

/*
Processes feedback coming from BrickServer
*/
void NahsBricksFeatureTemp::feedback(JsonDocument* in_json) {
    // check if new sensorPrecision value is delivered
    if (in_json->containsKey("p")) {
        uint8_t p = in_json->operator[]("p").as<uint8_t>();
        if (p >= 9 and p <=12) {
            RTCdata->sensorPrecision = p;
            _transmitPrecisionToSensors(true);
        }
    }

    // evaluate requests
    if (in_json->containsKey("r")) {
        for (JsonVariant value : in_json->operator[]("r").as<JsonArray>()) {
            switch(value.as<uint8_t>()) {
                case 4:
                    RTCdata->sensorCorrRequested = true;
                    break;
                case 6:
                    RTCdata->precisionRequested = true;
                    break;
            }
        }
    }
}

/*
Finalizes feature (closes stuff)
*/
void NahsBricksFeatureTemp::end() {
}

/*
Prints the features RTCdata in a formatted way to Serial (used for brickSetup)
*/
void NahsBricksFeatureTemp::printRTCdata() {
    Serial.print("  precisionRequested: ");
    SerHelp.printlnBool(RTCdata->precisionRequested);
    Serial.print("  sensorCorrRequested: ");
    SerHelp.printlnBool(RTCdata->sensorCorrRequested);
    Serial.print("  sensorPrecision: ");
    Serial.println(RTCdata->sensorPrecision);
    Serial.print("  sensorCount: ");
    Serial.println(RTCdata->sensorCount);
    Serial.println("  sensor (correction): ");
    if (_HDC1080_connected) {
        Serial.print("    ");
        Serial.print(HDC1080.snToString(RTCdata->HDC1080SN));
        Serial.print(" (");
        Serial.print(RTCdata->HDC1080Corr);
        Serial.println(")");
    }
    if (_SHT4x_connected) {
        Serial.print("    ");
        Serial.print(SHT4x.snToString(SHTdata->SHT4xSN));
        Serial.print(" (");
        Serial.print(SHTdata->SHT4xCorr);
        Serial.println(")");
    }
    for (uint8_t i = 0; i < RTCdata->sensorCount; ++i) {
        Serial.print("    ");
        Serial.print(_deviceAddrToString(i));
        Serial.print(" (");
        Serial.print(SCdata->sensorCorr[i]);
        Serial.println(")");
    }
}

/*
Prints the features FSdata in a formatted way to Serial (used for brickSetup)
*/
void NahsBricksFeatureTemp::printFSdata() {
    Serial.print("  defaultSensorPrecision: ");
    Serial.println(FSdata["sPrec"].as<uint8_t>());
    Serial.println("  default sensor corrections:");
    for (JsonPair kv : FSdata["sCorr"].as<JsonObject>()) {
        Serial.print("    ");
        Serial.print(kv.key().c_str());
        Serial.print(": ");
        Serial.println(kv.value().as<float>());
    }
}

/*
BrickSetup hands over to this function, when features-submenu is selected
*/
void NahsBricksFeatureTemp::brickSetupHandover() {
    _printMenu();
    while (true) {
        Serial.println();
        Serial.print("Select: ");
        uint8_t input = SerHelp.readLine().toInt();
        switch(input) {
            case 1:
                _identifySensor();
                break;
            case 2:
                _readSensorsRaw();
                break;
            case 3:
                _readSensorsCorr();
                break;
            case 4:
                _setDefaultPrecision();
                break;
            case 5:
                _setDefaultCorr();
                break;
            case 6:
                _deleteDefaultCorr();
                break;
            case 9:
                Serial.println("Returning to MainMenu!");
                return;
                break;
            default:
                Serial.println("Invalid input!");
                _printMenu();
                break;
        }
    }
}

/*
Brick-Specific helper to set pin where sensors are connected
*/
void NahsBricksFeatureTemp::setSensorsPin(uint8_t pin) {
    _oneWirePin = pin;
}

/*
Helper to convert a sensor address (identified by index in RTCmem) to a String
*/
String NahsBricksFeatureTemp::_deviceAddrToString(uint8_t sensor_index) {
    if (sensor_index < 4) return _deviceAddrToString(SAdata1->sensorAddr[sensor_index]);
    else return _deviceAddrToString(SAdata2->sensorAddr[sensor_index]);
}

/*
Helper to convert a sensor's DeviceAddress to a String
*/
String NahsBricksFeatureTemp::_deviceAddrToString(DeviceAddress deviceAddress) {
    String str = "";
    for (uint8_t i = 0; i < 8; ++i) {
        if (deviceAddress[i] < 16 ) str += String(0, HEX);
        str += String(deviceAddress[i], HEX);
    }
    return str;
}

/*
Helper to fetch the temperature of a sensor (identified by index in RTCmem)
*/
float NahsBricksFeatureTemp::_getTempC(uint8_t sensor_index) {
    if (sensor_index < 4) return _DS18B20.getTempC(SAdata1->sensorAddr[sensor_index]);
    else return _DS18B20.getTempC(SAdata2->sensorAddr[sensor_index]);
}

/*
Helper to configure the precision of sensors
*/
void NahsBricksFeatureTemp::_transmitPrecisionToSensors(bool inBackground) {
    _DS18B20.setResolution(RTCdata->sensorPrecision);
    _DS18B20.setWaitForConversion(!inBackground);
    _DS18B20.requestTemperatures();  // Request the temperatures once to be dumped, as the first reading allways returns 85C
}

/*
Helper to print Feature submenu during BrickSetup
*/
void NahsBricksFeatureTemp::_printMenu() {
    Serial.println("1) Identify sensor");
    Serial.println("2) Read sensors (raw)");
    Serial.println("3) Read sensors (with corr)");
    Serial.println("4) Set default precision");
    Serial.println("5) Set sensor corr");
    Serial.println("6) Delete sensor corr");
    Serial.println("9) Return to MainMenu");
}

/*
BrickSetup function to identify a sensor by touching it
*/
void NahsBricksFeatureTemp::_identifySensor() {
    Serial.println("Reading initial Temperatures...");
    _DS18B20.setWaitForConversion(true);
    _DS18B20.requestTemperatures();
    float iniTemps[RTCdata->sensorCount];
    for(uint8_t i = 0; i < RTCdata->sensorCount; i++) iniTemps[i] = _getTempC(i);
    float iniTempHDC = 0;
    if (_HDC1080_connected) {
        HDC1080.getT();  // dummy read to be able to trigger an new conversion
        HDC1080.triggerRead();
        iniTempHDC = HDC1080.getT();
    }
    float iniTempSHT = 0;
    if (_SHT4x_connected) {
        SHT4x.getT();  // dummy read to be able to trigger an new conversion
        SHT4x.triggerRead();
        iniTempSHT = SHT4x.getT();
    }

    Serial.println("Hit <enter> to start identification");
    SerHelp.readLine();
    Serial.println("--------------------");
    bool finished = false;
    for(uint8_t w = 0; w < 20; w++) {
        _DS18B20.requestTemperatures();
        if (_HDC1080_connected) {
            HDC1080.triggerRead();
            if((HDC1080.getT() - iniTempHDC) >= 2) {
                Serial.println();
                Serial.print("ID of Sensor is: ");
                Serial.println(HDC1080.snToString(RTCdata->HDC1080SN));
                finished = true;
            }
        }
        if (_SHT4x_connected) {
            SHT4x.triggerRead();
            if((SHT4x.getT() - iniTempSHT) >= 2) {
                Serial.println();
                Serial.print("ID of Sensor is: ");
                Serial.println(SHT4x.snToString(SHTdata->SHT4xSN));
                finished = true;
            }
        }
        for(uint8_t i = 0; i < RTCdata->sensorCount; i++) {
            float t = _getTempC(i);
            if((t - iniTemps[i]) >= 2) {
                Serial.println();
                Serial.print("ID of Sensor is: ");
                Serial.println(_deviceAddrToString(i));
                finished = true;
                break;
            }
        }
        if(finished) break;
        Serial.print('.');
        delay(300);
    }
    if(!finished) {
        Serial.println("\nNo Sensor could be identified...");
    }
}

/*
BrickSetup function to do a temp-reading on all sensors (and put it out WITHOUT correction value)
*/
void NahsBricksFeatureTemp::_readSensorsRaw() {
    Serial.println("Requesting Temperatures...");
    _DS18B20.setWaitForConversion(true);
    _DS18B20.requestTemperatures();
    if (_HDC1080_connected) {
        HDC1080.getT();  // dummy read to be able to trigger an new conversion
        HDC1080.triggerRead();
        Serial.print(HDC1080.snToString(RTCdata->HDC1080SN));
        Serial.print(": ");
        Serial.println(HDC1080.getT());
    }
    if (_SHT4x_connected) {
        SHT4x.getT();  // dummy read to be able to trigger an new conversion
        SHT4x.triggerRead();
        Serial.print(SHT4x.snToString(SHTdata->SHT4xSN));
        Serial.print(": ");
        Serial.println(SHT4x.getT());
    }
    for(uint8_t i = 0; i < RTCdata->sensorCount; ++i) {
        Serial.print(_deviceAddrToString(i));
        Serial.print(": ");
        Serial.println(_getTempC(i));
    }
}

/*
BrickSetup function to do a temp-reading on all sensors (and put it out WITH correction value)
*/
void NahsBricksFeatureTemp::_readSensorsCorr() {
    Serial.println("Requesting Temperatures...");
    _DS18B20.setWaitForConversion(true);
    _DS18B20.requestTemperatures();
    if (_HDC1080_connected) {
        HDC1080.getT();  // dummy read to be able to trigger an new conversion
        HDC1080.triggerRead();
        Serial.print(HDC1080.snToString(RTCdata->HDC1080SN));
        Serial.print(": ");
        Serial.println(HDC1080.getT() + RTCdata->HDC1080Corr);
    }
    if (_SHT4x_connected) {
        SHT4x.getT();  // dummy read to be able to trigger an new conversion
        SHT4x.triggerRead();
        Serial.print(SHT4x.snToString(SHTdata->SHT4xSN));
        Serial.print(": ");
        Serial.println(SHT4x.getT() + SHTdata->SHT4xCorr);
    }
    for(uint8_t i = 0; i < RTCdata->sensorCount; ++i) {
        Serial.print(_deviceAddrToString(i));
        Serial.print(": ");
        Serial.println((_getTempC(i) + SCdata->sensorCorr[i]));
    }
}

/*
BrickSetup function to set the default precision
*/
void NahsBricksFeatureTemp::_setDefaultPrecision() {
    Serial.print("Enter precision (8 to 12): ");
    uint8_t input = SerHelp.readLine().toInt();
    if (input < 8 || input > 12) {
        Serial.println("Invalid precision!");
        return;
    }
    FSdata["sPrec"] = input;
    RTCdata->sensorPrecision = input;
    Serial.println("Configuring sensors...");
    _transmitPrecisionToSensors(false);
    Serial.print("Set precision to: ");
    Serial.println(input);
}

/*
BrickSetup function to set a default correction value for a sensor
*/
void NahsBricksFeatureTemp::_setDefaultCorr() {
    Serial.print("Enter sensor ID: ");
    String addr = SerHelp.readLine();
    Serial.print("Enter correction value: ");
    float corr = SerHelp.readLine().toFloat();

    if (HDC1080.snToString(RTCdata->HDC1080SN) == addr) {
        RTCdata->HDC1080Corr = corr;
    }
    else if (SHT4x.snToString(SHTdata->SHT4xSN) == addr) {
        SHTdata->SHT4xCorr = corr;
    }
    else for (uint8_t i = 0; i < RTCdata->sensorCount; ++i) {
        if (_deviceAddrToString(i) == addr) {
            SCdata->sensorCorr[i] = corr;
            break;
        }
    }
    FSdata["sCorr"].as<JsonObject>()[addr] = corr;
    Serial.print("Set correction value of ");
    Serial.print(addr);
    Serial.print(" to ");
    Serial.println(corr);
}

/*
BrickSetup function to delete a default correction value for a sensor
*/
void NahsBricksFeatureTemp::_deleteDefaultCorr() {
    Serial.print("Enter sensor ID: ");
    String addr = SerHelp.readLine();

    if (HDC1080.snToString(RTCdata->HDC1080SN) == addr) {
        RTCdata->HDC1080Corr = 0;
    }
    else if (SHT4x.snToString(SHTdata->SHT4xSN) == addr) {
        SHTdata->SHT4xCorr = 0;
    }
    else for (uint8_t i = 0; i < RTCdata->sensorCount; ++i) {
        if (_deviceAddrToString(i) == addr) {
            SCdata->sensorCorr[i] = 0;
            break;
        }
    }

    if (FSdata["sCorr"].as<JsonObject>().containsKey(addr)) FSdata["sCorr"].as<JsonObject>().remove(addr);
    Serial.print("Deleted correction value of ");
    Serial.println(addr);
}


//------------------------------------------
// globally predefined variable
#if !defined(NO_GLOBAL_INSTANCES)
NahsBricksFeatureTemp FeatureTemp;
#endif
