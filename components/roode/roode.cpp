#include "esphome/core/log.h"
#include "roode.h"
namespace esphome
{
    namespace roode
    {
        void Roode::dump_config()
        {
            ESP_LOGCONFIG(TAG, "dump config:");
            LOG_I2C_DEVICE(this);

            LOG_UPDATE_INTERVAL(this);
        }
        void Roode::setup()
        {
            ESP_LOGI(SETUP, "Booting Roode %s", VERSION);
            if (version_sensor != nullptr)
            {
                version_sensor->publish_state(VERSION);
            }
            Wire.begin();
            Wire.setClock(400000);

            // Initialize the sensor, give the special I2C_address to the Begin function
            // Set a different I2C address
            // This address is stored as long as the sensor is powered. To revert this change you can unplug and replug the power to the sensor
            distanceSensor.SetI2CAddress(VL53L1X_ULD_I2C_ADDRESS);

            sensor_status = distanceSensor.Begin(VL53L1X_ULD_I2C_ADDRESS);
            if (sensor_status != VL53L1_ERROR_NONE)
            {
                // If the sensor could not be initialized print out the error code. -7 is timeout
                ESP_LOGE(SETUP, "Could not initialize the sensor, error code: %d", sensor_status);
                while (1)
                {
                }
            }
            if (sensor_offset_calibration_ != -1)
            {
                ESP_LOGI(CALIBRATION, "Setting sensor offset calibration to %d", sensor_offset_calibration_);
                sensor_status = distanceSensor.SetOffsetInMm(sensor_offset_calibration_);
                if (sensor_status != VL53L1_ERROR_NONE)
                {
                    ESP_LOGE(SETUP, "Could not set sensor offset calibration, error code: %d", sensor_status);
                    while (1)
                    {
                    }
                }
            }
            if (sensor_xtalk_calibration_ != -1)
            {
                ESP_LOGI(CALIBRATION, "Setting sensor xtalk calibration to %d", sensor_xtalk_calibration_);
                sensor_status = distanceSensor.SetXTalk(sensor_xtalk_calibration_);
                if (sensor_status != VL53L1_ERROR_NONE)
                {
                    ESP_LOGE(SETUP, "Could not set sensor offset calibration, error code: %d", sensor_status);
                    while (1)
                    {
                    }
                }
            }
            ESP_LOGI(SETUP, "Using sampling with sampling size: %d", DISTANCES_ARRAY_SIZE);
            ESP_LOGI(SETUP, "Creating entry and exit zones.");
            createEntryAndExitZone();

            if (calibration_active_)
            {
                calibration(distanceSensor);
                App.feed_wdt();
            }
            if (manual_active_)
            {
                ESP_LOGI(SETUP, "Manual sensor configuration");
                sensorConfiguration.setSensorMode(distanceSensor, sensor_mode, timing_budget_);
                entry->setMaxThreshold(manual_threshold_);
                exit->setMaxThreshold(manual_threshold_);
                publishSensorConfiguration(entry, exit, true);
            }
            distanceSensor.SetInterMeasurementInMs(delay_between_measurements);
            distanceSensor.StartRanging();
        }

        void Roode::update()
        {
            if (distance_entry != nullptr)
            {
                distance_entry->publish_state(entry->getDistance());
            }
            if (distance_exit != nullptr)
            {
                distance_exit->publish_state(exit->getDistance());
            }
        }

        void Roode::loop()
        {
            // unsigned long start = micros();
            distance = getAlternatingZoneDistances();
            doPathTracking(this->current_zone);
            handleSensorStatus();
            this->current_zone = this->current_zone == this->entry ? this->exit : this->entry;
            // ESP_LOGI("Experimental", "Entry zone: %d, exit zone: %d", entry->getDistance(Roode::distanceSensor, Roode::sensor_status), exit->getDistance(Roode::distanceSensor, Roode::sensor_status));
            // unsigned long end = micros();
            // unsigned long delta = end - start;
            // ESP_LOGI("Roode loop", "loop took %lu microseconds", delta);
        }

        bool Roode::handleSensorStatus()
        {
            ESP_LOGD(TAG, "Sensor status: %d, Last sensor status: %d", sensor_status, last_sensor_status);
            bool check_status = false;
            if (last_sensor_status == sensor_status && sensor_status == VL53L1_ERROR_NONE)
            {
                if (status_sensor != nullptr)
                {
                    status_sensor->publish_state(sensor_status);
                }
                check_status = true;
            }
            if (sensor_status < 28 && sensor_status != VL53L1_ERROR_NONE)
            {
                ESP_LOGE(TAG, "Ranging failed with an error. status: %d", sensor_status);
                status_sensor->publish_state(sensor_status);
                check_status = false;
            }

            last_sensor_status = sensor_status;
            sensor_status = VL53L1_ERROR_NONE;
            return check_status;
        }

        void Roode::createEntryAndExitZone()
        {
            if (advised_sensor_orientation_)
            {
                entry = new Zone(entry_roi_width, entry_roi_height, 167);
                exit = new Zone(exit_roi_width, exit_roi_height, 231);
            }
            else
            {
                entry = new Zone(entry_roi_width, entry_roi_height, 195);
                exit = new Zone(exit_roi_width, exit_roi_height, 60);
            }

            current_zone = entry;
        }
        uint16_t Roode::getAlternatingZoneDistances()
        {
            this->current_zone->readDistance(distanceSensor);
            App.feed_wdt();
            return this->current_zone->getDistance();
        }

        void Roode::doPathTracking(Zone *zone)
        {
            static int PathTrack[] = {0, 0, 0, 0};
            static int PathTrackFillingSize = 1; // init this to 1 as we start from state where nobody is any of the zones
            static int LeftPreviousStatus = NOBODY;
            static int RightPreviousStatus = NOBODY;
            static uint8_t DistancesTableSize[2] = {0, 0};
            int CurrentZoneStatus = NOBODY;
            int AllZonesCurrentStatus = 0;
            int AnEventHasOccured = 0;

            uint16_t Distances[2][DISTANCES_ARRAY_SIZE];
            uint16_t MinDistance;
            uint8_t i;
            if (DistancesTableSize[zone->getZoneId()] < DISTANCES_ARRAY_SIZE)
            {
                ESP_LOGD(TAG, "Distances[%d][DistancesTableSize[zone]] = %d", zone->getZoneId(), distance);
                Distances[zone->getZoneId()][DistancesTableSize[zone->getZoneId()]] = zone->getDistance();
                DistancesTableSize[zone->getZoneId()]++;
            }
            else
            {
                for (i = 1; i < DISTANCES_ARRAY_SIZE; i++)
                    Distances[zone->getZoneId()][i - 1] = Distances[zone->getZoneId()][i];
                Distances[zone->getZoneId()][DISTANCES_ARRAY_SIZE - 1] = distance;
                ESP_LOGD(SETUP, "Distances[%d][DISTANCES_ARRAY_SIZE - 1] = %d", zone->getZoneId(), Distances[zone->getZoneId()][DISTANCES_ARRAY_SIZE - 1]);
            }
            ESP_LOGD(SETUP, "Distances[%d][0]] = %d", zone->getZoneId(), Distances[zone->getZoneId()][0]);
            ESP_LOGD(SETUP, "Distances[%d][1]] = %d", zone->getZoneId(), Distances[zone->getZoneId()][1]);
            // pick up the min distance
            MinDistance = Distances[zone->getZoneId()][0];
            if (DistancesTableSize[zone->getZoneId()] >= 2)
            {
                for (i = 1; i < DistancesTableSize[zone->getZoneId()]; i++)
                {
                    if (Distances[zone->getZoneId()][i] < MinDistance)
                        MinDistance = Distances[zone->getZoneId()][i];
                }
            }
            distance = MinDistance;

            // PathTrack algorithm
            if (distance < zone->getMaxThreshold() && distance > zone->getMinThreshold())
            {
                // Someone is in the sensing area
                CurrentZoneStatus = SOMEONE;
                if (presence_sensor != nullptr)
                {
                    presence_sensor->publish_state(true);
                }
            }

            // left zone
            if (zone == (this->invert_direction_ ? this->exit : this->entry))
            {
                if (CurrentZoneStatus != LeftPreviousStatus)
                {
                    // event in left zone has occured
                    AnEventHasOccured = 1;

                    if (CurrentZoneStatus == SOMEONE)
                    {
                        AllZonesCurrentStatus += 1;
                    }
                    // need to check right zone as well ...
                    if (RightPreviousStatus == SOMEONE)
                    {
                        // event in right zone has occured
                        AllZonesCurrentStatus += 2;
                    }
                    // remember for next time
                    LeftPreviousStatus = CurrentZoneStatus;
                }
            }
            // right zone
            else
            {
                if (CurrentZoneStatus != RightPreviousStatus)
                {

                    // event in right zone has occured
                    AnEventHasOccured = 1;
                    if (CurrentZoneStatus == SOMEONE)
                    {
                        AllZonesCurrentStatus += 2;
                    }
                    // need to check left zone as well ...
                    if (LeftPreviousStatus == SOMEONE)
                    {
                        // event in left zone has occured
                        AllZonesCurrentStatus += 1;
                    }
                    // remember for next time
                    RightPreviousStatus = CurrentZoneStatus;
                }
            }

            // if an event has occured
            if (AnEventHasOccured)
            {
                if (PathTrackFillingSize < 4)
                {
                    PathTrackFillingSize++;
                }

                // if nobody anywhere lets check if an exit or entry has happened
                if ((LeftPreviousStatus == NOBODY) && (RightPreviousStatus == NOBODY))
                {

                    // check exit or entry only if PathTrackFillingSize is 4 (for example 0 1 3 2) and last event is 0 (nobobdy anywhere)
                    if (PathTrackFillingSize == 4)
                    {
                        // check exit or entry. no need to check PathTrack[0] == 0 , it is always the case

                        if ((PathTrack[1] == 1) && (PathTrack[2] == 3) && (PathTrack[3] == 2))
                        {
                            // This an exit
                            ESP_LOGD("Roode pathTracking", "Exit detected.");
                            DistancesTableSize[0] = 0;
                            DistancesTableSize[1] = 0;
                            this->updateCounter(-1);
                            if (entry_exit_event_sensor != nullptr)
                            {
                                entry_exit_event_sensor->publish_state("Exit");
                            }
                        }
                        else if ((PathTrack[1] == 2) && (PathTrack[2] == 3) && (PathTrack[3] == 1))
                        {
                            // This an entry
                            ESP_LOGD("Roode pathTracking", "Entry detected.");
                            this->updateCounter(1);
                            if (entry_exit_event_sensor != nullptr)
                            {
                                entry_exit_event_sensor->publish_state("Entry");
                            }
                            DistancesTableSize[0] = 0;
                            DistancesTableSize[1] = 0;
                        }
                        else
                        {
                            // reset the table filling size also in case of unexpected path
                            DistancesTableSize[0] = 0;
                            DistancesTableSize[1] = 0;
                        }
                    }

                    PathTrackFillingSize = 1;
                }
                else
                {
                    // update PathTrack
                    // example of PathTrack update
                    // 0
                    // 0 1
                    // 0 1 3
                    // 0 1 3 1
                    // 0 1 3 3
                    // 0 1 3 2 ==> if next is 0 : check if exit
                    PathTrack[PathTrackFillingSize - 1] = AllZonesCurrentStatus;
                }
            }
            if (presence_sensor != nullptr)
            {
                if (CurrentZoneStatus == NOBODY && LeftPreviousStatus == NOBODY && RightPreviousStatus == NOBODY)
                {
                    // nobody is in the sensing area
                    presence_sensor->publish_state(false);
                }
            }
        }
        void Roode::updateCounter(int delta)
        {
            if (this->people_counter == nullptr)
            {
                return;
            }
            auto next = this->people_counter->state + (float)delta;
            ESP_LOGI(TAG, "Updating people count: %d", (int)next);
            this->people_counter->set(next);
        }
        void Roode::recalibration()
        {
            calibration(distanceSensor);
        }
        void Roode::roi_calibration(VL53L1X_ULD distanceSensor, int optimized_zone_0, int optimized_zone_1)
        {
            // the value of the average distance is used for computing the optimal size of the ROI and consequently also the center of the two zones
            int function_of_the_distance = 16 * (1 - (0.15 * 2) / (0.34 * (min(optimized_zone_0, optimized_zone_1) / 1000)));
            int ROI_size = min(8, max(4, function_of_the_distance));

            entry->updateRoi(ROI_size, ROI_size * 2);
            exit->updateRoi(ROI_size, ROI_size * 2);
            // now we set the position of the center of the two zones
            if (advised_sensor_orientation_)
            {
                switch (ROI_size)
                {
                case 4:
                    entry->setRoiCenter(150);
                    exit->setRoiCenter(247);
                    break;
                case 5:
                    entry->setRoiCenter(159);
                    exit->setRoiCenter(239);
                    break;
                case 6:
                    entry->setRoiCenter(159);
                    exit->setRoiCenter(239);
                    break;
                case 7:
                    entry->setRoiCenter(167);
                    exit->setRoiCenter(231);
                    break;
                case 8:
                    entry->setRoiCenter(167);
                    exit->setRoiCenter(231);
                    break;
                }
            }
            else
            {
                switch (ROI_size)
                {
                case 4:
                    entry->setRoiCenter(193);
                    exit->setRoiCenter(58);
                    break;
                case 5:
                    entry->setRoiCenter(194);
                    exit->setRoiCenter(59);
                    break;
                case 6:
                    entry->setRoiCenter(194);
                    exit->setRoiCenter(59);
                    break;
                case 7:
                    entry->setRoiCenter(195);
                    exit->setRoiCenter(60);
                    break;
                case 8:
                    entry->setRoiCenter(195);
                    exit->setRoiCenter(60);
                    break;
                }
            }
            // we will now repeat the calculations necessary to define the thresholds with the updated zones
            int *values_zone_0 = new int[number_attempts];
            int *values_zone_1 = new int[number_attempts];

            distanceSensor.SetInterMeasurementInMs(delay_between_measurements);
            current_zone = entry;
            for (int i = 0; i < number_attempts; i++)
            {
                values_zone_0[i] = getAlternatingZoneDistances();
                values_zone_1[i] = getAlternatingZoneDistances();
            }

            optimized_zone_0 = getOptimizedValues(values_zone_0, getSum(values_zone_0, number_attempts), number_attempts);
            optimized_zone_1 = getOptimizedValues(values_zone_1, getSum(values_zone_1, number_attempts), number_attempts);
        }
        void Roode::setSensorMode(int sensor_mode, int new_timing_budget)
        {
            distanceSensor.StopRanging();
            switch (sensor_mode)
            {
            case 0: // short mode
                time_budget_in_ms = time_budget_in_ms_short;
                delay_between_measurements = time_budget_in_ms + 5;
                sensor_status = distanceSensor.SetDistanceMode(Short);
                if (sensor_status != VL53L1_ERROR_NONE)
                {
                    ESP_LOGE(SETUP, "Could not set distance mode.  mode: %d", Short);
                }
                ESP_LOGI(SETUP, "Set short mode. timing_budget: %d", time_budget_in_ms);
                break;
            case 1: // medium mode
                time_budget_in_ms = time_budget_in_ms_medium;
                delay_between_measurements = time_budget_in_ms + 5;
                sensor_status = distanceSensor.SetDistanceMode(Long);
                if (sensor_status != VL53L1_ERROR_NONE)
                {
                    ESP_LOGE(SETUP, "Could not set distance mode.  mode: %d", Long);
                }
                ESP_LOGI(SETUP, "Set medium mode. timing_budget: %d", time_budget_in_ms);
                break;
            case 2: // medium_long mode
                time_budget_in_ms = time_budget_in_ms_medium_long;
                delay_between_measurements = time_budget_in_ms + 5;
                sensor_status = distanceSensor.SetDistanceMode(Long);
                if (sensor_status != VL53L1_ERROR_NONE)
                {
                    ESP_LOGE(SETUP, "Could not set distance mode.  mode: %d", Long);
                }
                ESP_LOGI(SETUP, "Set medium long range mode. timing_budget: %d", time_budget_in_ms);
                break;
            case 3: // long mode
                time_budget_in_ms = time_budget_in_ms_long;
                delay_between_measurements = time_budget_in_ms + 5;
                sensor_status = distanceSensor.SetDistanceMode(Long);
                if (sensor_status != VL53L1_ERROR_NONE)
                {
                    ESP_LOGE(SETUP, "Could not set distance mode.  mode: %d", Long);
                }
                ESP_LOGI(SETUP, "Set long range mode. timing_budget: %d", time_budget_in_ms);
                break;
            case 4: // max mode
                time_budget_in_ms = time_budget_in_ms_max;
                delay_between_measurements = time_budget_in_ms + 5;
                sensor_status = distanceSensor.SetDistanceMode(Long);
                if (sensor_status != VL53L1_ERROR_NONE)
                {
                    ESP_LOGE(SETUP, "Could not set distance mode.  mode: %d", Long);
                }
                ESP_LOGI(SETUP, "Set max range mode. timing_budget: %d", time_budget_in_ms);
                break;
            case 5: // custom mode
                time_budget_in_ms = new_timing_budget;
                delay_between_measurements = new_timing_budget + 5;
                sensor_status = distanceSensor.SetDistanceMode(Long);
                if (sensor_status != VL53L1_ERROR_NONE)
                {
                    ESP_LOGE(SETUP, "Could not set distance mode.  mode: %d", Long);
                }
                ESP_LOGI(SETUP, "Manually set custom range mode. timing_budget: %d", time_budget_in_ms);
                break;
            default:
                break;
            }
            sensor_status = distanceSensor.SetTimingBudgetInMs(time_budget_in_ms);
            if (sensor_status != 0)
            {
                ESP_LOGE(SETUP, "Could not set timing budget.  timing_budget: %d ms", time_budget_in_ms);
            }
        }

        void Roode::setCorrectDistanceSettings(float average_zone_0, float average_zone_1)
        {
            if (average_zone_0 <= short_distance_threshold || average_zone_1 <= short_distance_threshold)
            {
                setSensorMode(0);
            }

            if ((average_zone_0 > short_distance_threshold && average_zone_0 <= medium_distance_threshold) || (average_zone_1 > short_distance_threshold && average_zone_1 <= medium_distance_threshold))
            {
                setSensorMode(1);
            }

            if ((average_zone_0 > medium_distance_threshold && average_zone_0 <= medium_long_distance_threshold) || (average_zone_1 > medium_distance_threshold && average_zone_1 <= medium_long_distance_threshold))
            {
                setSensorMode(2);
            }
            if ((average_zone_0 > medium_long_distance_threshold && average_zone_0 <= long_distance_threshold) || (average_zone_1 > medium_long_distance_threshold && average_zone_1 <= long_distance_threshold))
            {
                setSensorMode(3);
            }
            if (average_zone_0 > long_distance_threshold || average_zone_1 > long_distance_threshold)
            {
                setSensorMode(4);
            }
        }

        int Roode::getSum(int *array, int size)
        {
            int sum = 0;
            for (int i = 0; i < size; i++)
            {
                sum = sum + array[i];
                App.feed_wdt();
            }
            return sum;
        }
        int Roode::getOptimizedValues(int *values, int sum, int size)
        {
            int sum_squared = 0;
            int variance = 0;
            int sd = 0;
            int avg = sum / size;

            for (int i = 0; i < size; i++)
            {
                sum_squared = sum_squared + (values[i] * values[i]);
                App.feed_wdt();
            }
            variance = sum_squared / size - (avg * avg);
            sd = sqrt(variance);
            ESP_LOGD(CALIBRATION, "Zone AVG: %d", avg);
            ESP_LOGD(CALIBRATION, "Zone 0 SD: %d", sd);
            return avg - sd;
        }

        void Roode::calibration(VL53L1X_ULD distanceSensor)
        {
            ESP_LOGI(SETUP, "Calibrating sensor");
            distanceSensor.StopRanging();
            // the sensor does 100 measurements for each zone (zones are predefined)
            time_budget_in_ms = time_budget_in_ms_medium;
            delay_between_measurements = time_budget_in_ms + 5;
            distanceSensor.SetDistanceMode(Long);
            sensor_status = distanceSensor.SetTimingBudgetInMs(time_budget_in_ms);

            if (sensor_status != VL53L1_ERROR_NONE)
            {
                ESP_LOGE(CALIBRATION, "Could not set timing budget. timing_budget: %d ms, status: %d", time_budget_in_ms, sensor_status);
            }
            entry->updateRoi(entry_roi_width, entry_roi_height);
            exit->updateRoi(exit_roi_width, exit_roi_height);

            int *values_zone_0 = new int[number_attempts];
            int *values_zone_1 = new int[number_attempts];
            distanceSensor.SetInterMeasurementInMs(delay_between_measurements);
            current_zone = entry;
            for (int i = 0; i < number_attempts; i++)
            {
                values_zone_0[i] = getAlternatingZoneDistances();
                values_zone_1[i] = getAlternatingZoneDistances();
            }

            // after we have computed the sum for each zone, we can compute the average distance of each zone

            optimized_zone_0 = getOptimizedValues(values_zone_0, getSum(values_zone_0, number_attempts), number_attempts);
            optimized_zone_1 = getOptimizedValues(values_zone_1, getSum(values_zone_1, number_attempts), number_attempts);
            setCorrectDistanceSettings(optimized_zone_0, optimized_zone_1);
            if (roi_calibration_)
            {
                roi_calibration(distanceSensor, optimized_zone_0, optimized_zone_1);
            }

            entry->setMaxThreshold(optimized_zone_0 * max_threshold_percentage_ / 100); // they can be int values, as we are not interested in the decimal part when defining the threshold
            exit->setMaxThreshold(optimized_zone_1 * max_threshold_percentage_ / 100);
            int hundred_threshold_zone_0 = entry->getMaxThreshold() / 100;
            int hundred_threshold_zone_1 = exit->getMaxThreshold() / 100;
            int unit_threshold_zone_0 = entry->getMaxThreshold() - 100 * hundred_threshold_zone_0;
            int unit_threshold_zone_1 = exit->getMaxThreshold() - 100 * hundred_threshold_zone_1;
            publishSensorConfiguration(entry, exit, true);
            App.feed_wdt();
            if (min_threshold_percentage_ != 0)
            {
                entry->setMinThreshold(optimized_zone_0 * min_threshold_percentage_ / 100); // they can be int values, as we are not interested in the decimal part when defining the threshold
                exit->setMinThreshold(optimized_zone_1 * min_threshold_percentage_ / 100);
                publishSensorConfiguration(entry, exit, false);
            }
            distanceSensor.StopRanging();
        }

        void Roode::publishSensorConfiguration(Zone *entry, Zone *exit, bool isMax)
        {
            if (isMax)
            {
                ESP_LOGI(SETUP, "Max threshold entry: %dmm", entry->getMaxThreshold());
                ESP_LOGI(SETUP, "Max threshold exit: %dmm", exit->getMaxThreshold());
                if (max_threshold_entry_sensor != nullptr)
                {
                    max_threshold_entry_sensor->publish_state(entry->getMaxThreshold());
                }

                if (max_threshold_exit_sensor != nullptr)
                {
                    max_threshold_exit_sensor->publish_state(exit->getMaxThreshold());
                }
            }
            else
            {
                ESP_LOGI(SETUP, "Min threshold entry: %dmm", entry->getMaxThreshold());
                ESP_LOGI(SETUP, "Min threshold exit: %dmm", exit->getMaxThreshold());
                if (min_threshold_entry_sensor != nullptr)
                {
                    min_threshold_entry_sensor->publish_state(entry->getMinThreshold());
                }
                if (min_threshold_exit_sensor != nullptr)
                {
                    min_threshold_exit_sensor->publish_state(exit->getMinThreshold());
                }
            }

            if (entry_roi_height_sensor != nullptr)
            {
                entry_roi_height_sensor->publish_state(entry->getRoiHeight());
            }
            if (entry_roi_width_sensor != nullptr)
            {
                entry_roi_width_sensor->publish_state(entry->getRoiWidth());
            }

            if (exit_roi_height_sensor != nullptr)
            {
                exit_roi_height_sensor->publish_state(exit->getRoiHeight());
            }
            if (exit_roi_width_sensor != nullptr)
            {
                exit_roi_width_sensor->publish_state(exit->getRoiWidth());
            }
        }
    }
}