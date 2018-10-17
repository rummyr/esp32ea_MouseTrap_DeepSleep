#include "esp_attr.h" // for RTC_DATA_ATTR
#include "driver/gpio.h" // for gpio_num_t
 
RTC_DATA_ATTR bool debugWakeStub = false;
RTC_DATA_ATTR bool flushUARTAtEndOfWakeStub = false;

RTC_DATA_ATTR uint32_t stubWakeCount = 0;
RTC_DATA_ATTR uint64_t stubCumulativeWakeTimeMicros = 0;
RTC_DATA_ATTR uint64_t stubLastWakeTimeMicros = 0;
RTC_DATA_ATTR uint32_t stubPollTimeSecs = 3;
RTC_DATA_ATTR gpio_num_t inputPin = GPIO_NUM_15;
RTC_DATA_ATTR gpio_num_t outputPin = GPIO_NUM_MAX;
RTC_DATA_ATTR gpio_num_t ledPin = GPIO_NUM_5; 
RTC_DATA_ATTR long lingerInWakeupStub = 0;

// There are THREE precalculations that must be done
RTC_DATA_ATTR uint64_t precalulated_rtc_count_delta_per_sec = 0;
RTC_DATA_ATTR uint64_t precalulated_rtc_count_delta_constant_offset = 0;
RTC_DATA_ATTR uint32_t  precalculated_clk_slowclk_cal_get =  1;



