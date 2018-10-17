#include <Arduino.h>
#include "driver/rtc_io.h"

#include "soc/timer_group_reg.h" // TIMG_WDTFEED_REG
#include "soc/uart_reg.h" // UART_STATUS_REG
#include "rom/rtc.h" // RTC_ENTRY_ADDR_REG
#include "soc/rtc.h" // RTC_CLK_CAL_FRACT and others
#include "esp_clk.h"


#define DBG_PRINTS_IN_WAKEUP1(a)         do { static RTC_RODATA_ATTR const char fmt[] = a; if (debugWakeStub) { ets_printf(fmt); flushUART(); }} while (0)
#define DBG_PRINTS_IN_WAKEUP2(a,b)       do { static RTC_RODATA_ATTR const char fmt[] = a; if (debugWakeStub) { ets_printf(fmt,b); flushUART();}} while (0)
#define DBG_PRINTS_IN_WAKEUP3(a,b,c)     do { static RTC_RODATA_ATTR const char fmt[] = a; if (debugWakeStub) { ets_printf(fmt,b,c); flushUART();}} while (0) 
#define DBG_PRINTS_IN_WAKEUP4(a,b,c,d)   do { static RTC_RODATA_ATTR const char fmt[] = a; if (debugWakeStub) { ets_printf(fmt,b,c,d); flushUART();}} while (0) 
#define DBG_PRINTS_IN_WAKEUP5(a,b,c,d,e) do { static RTC_RODATA_ATTR const char fmt[] = a; if (debugWakeStub) { ets_printf(fmt,b,c,d,e); flushUART();}}} while (0) 

#define uS_TO_S_FACTOR 1000000LL  /* Conversion factor for micro seconds to seconds , */

// EXTERNAL VARIABLES
extern RTC_DATA_ATTR bool debugWakeStub;
extern RTC_DATA_ATTR bool flushUARTAtEndOfWakeStub;

extern RTC_DATA_ATTR int stubWakeCount;
extern RTC_DATA_ATTR uint64_t stubCumulativeWakeTimeMicros;
extern RTC_DATA_ATTR uint64_t stubLastWakeTimeMicros;
extern RTC_DATA_ATTR uint32_t stubPollTimeSecs;
extern RTC_DATA_ATTR gpio_num_t inputPin;
extern RTC_DATA_ATTR gpio_num_t outputPin;
extern RTC_DATA_ATTR gpio_num_t ledPin; 
extern RTC_DATA_ATTR long lingerInWakeupStub;
extern RTC_DATA_ATTR uint64_t precalulated_rtc_count_delta_per_sec;
extern RTC_DATA_ATTR uint64_t precalulated_rtc_count_delta_constant_offset;
extern RTC_DATA_ATTR uint32_t  precalculated_clk_slowclk_cal_get;






// PROTOTYPES
extern boolean RTC_IRAM_ATTR should_stub_wake_fully();
int RTC_IRAM_ATTR RTCIRAM_readGPIO(gpio_num_t gpioNum);
void RTC_IRAM_ATTR  RTCIRAM_setGPIOOutputState(int gpioNum, bool on);
uint64_t RTC_IRAM_ATTR my_rtc_time_get();
uint64_t RTC_IRAM_ATTR setDeepSleepTimerSecs(uint64_t time_in_sec);
void RTC_IRAM_ATTR my_rtc_sleep_set_wakeup_time(uint64_t t);
uint64_t RTC_IRAM_ATTR my_conv_rtc_time_us(uint64_t ticks); 
uint64_t RTC_IRAM_ATTR my_rtc_time_get();
boolean arePinsConnected(gpio_num_t in, gpio_num_t out);
void RTC_IRAM_ATTR flushUART();




// following defines are NOT in a header, and have no accessors
// Extra time it takes to enter and exit light sleep and deep sleep
// For deep sleep, this is until the wake stub runs (not the app).
#ifdef CONFIG_ESP32_RTC_CLOCK_SOURCE_EXTERNAL_CRYSTAL
#define LIGHT_SLEEP_TIME_OVERHEAD_US (650 + 30 * 240 / CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ)
#define DEEP_SLEEP_TIME_OVERHEAD_US (650 + 100 * 240 / CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ)
#else
#define LIGHT_SLEEP_TIME_OVERHEAD_US (250 + 30 * 240 / CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ)
#define DEEP_SLEEP_TIME_OVERHEAD_US (250 + 100 * 240 / CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ)
#endif // CONFIG_ESP32_RTC_CLOCK_SOURCE

/** Call this first! */
void wakeStubDoPreCalculations() {
    uint32_t sleep_time_adjustment = DEEP_SLEEP_TIME_OVERHEAD_US;

    precalculated_clk_slowclk_cal_get =  REG_READ(RTC_SLOW_CLK_CAL_REG); // aka period, and its never quite the same twice!
    precalulated_rtc_count_delta_per_sec = (uS_TO_S_FACTOR << (uint64_t)RTC_CLK_CAL_FRACT) / (uint64_t) precalculated_clk_slowclk_cal_get;
    precalulated_rtc_count_delta_constant_offset = ((uint64_t)sleep_time_adjustment << (uint64_t)RTC_CLK_CAL_FRACT) / (uint64_t) precalculated_clk_slowclk_cal_get;

}


//see https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/deep-sleep-stub.rst
void RTC_IRAM_ATTR esp_wake_deep_sleep(void) {
  stubWakeCount++;
  uint64_t dbg_rtc_ticks_at_wake_start; 
    
  static RTC_RODATA_ATTR const char fmt_wake_count[] = "\tSTUB Wake count %d\n";
  ets_printf(fmt_wake_count, stubWakeCount);

  dbg_rtc_ticks_at_wake_start = my_rtc_time_get();
  
  DBG_PRINTS_IN_WAKEUP2("\tCumulative Wake Micros:%llu\n", stubCumulativeWakeTimeMicros );
  DBG_PRINTS_IN_WAKEUP2("\tLast Wake Micros:%llu\n", stubLastWakeTimeMicros );
  
  DBG_PRINTS_IN_WAKEUP2("\tabout to set led to %d\n", stubWakeCount %2);
  RTCIRAM_setGPIOOutputState(ledPin, stubWakeCount %2);

  
  DBG_PRINTS_IN_WAKEUP1("\tabout to call should_stub_wake_fully\n");
  boolean shouldWake = should_stub_wake_fully();
  DBG_PRINTS_IN_WAKEUP1("\tcalled should_stub_wake_fully\n");
  

  if (shouldWake) {
    DBG_PRINTS_IN_WAKEUP1("\tFULL WAKEUP REQUESTED!\n");    
  } else {
    DBG_PRINTS_IN_WAKEUP1("\tabout to re-enter sleep from wakeup stub!\n");
  }

  if (!shouldWake) {
    if (lingerInWakeupStub > 0) {
      DBG_PRINTS_IN_WAKEUP1("\tlingering!!\n");
      int delayed = 0;
      while (delayed < lingerInWakeupStub) {
        ets_delay_us(1000); // delay safe for wakeup stub
        // feed the watchdog
        REG_WRITE(TIMG_WDTFEED_REG(0), 1); // in soc\timer_group_reg.h
        delayed += 1000;
      } // end if lingering
    } // end if linger time set


    uint64_t expectToWakeUpAt = setDeepSleepTimerSecs(stubPollTimeSecs);
    if (debugWakeStub) {
      uint64_t rtc_ticks_at_wake_endish = my_rtc_time_get();
  
      DBG_PRINTS_IN_WAKEUP4("\tsleeping, awake between %lld and %lld rtc ticks, expected to wake at %lld ticks\n"
        , dbg_rtc_ticks_at_wake_start , rtc_ticks_at_wake_endish , expectToWakeUpAt);
  
      DBG_PRINTS_IN_WAKEUP4("\tsleeping, awake between %lld and %lld micros, expected to wake at %lld uSec\n"
        , my_conv_rtc_time_us(dbg_rtc_ticks_at_wake_start) , my_conv_rtc_time_us(rtc_ticks_at_wake_endish), my_conv_rtc_time_us(expectToWakeUpAt) );
    }

    
    if (flushUARTAtEndOfWakeStub) {
      DBG_PRINTS_IN_WAKEUP1("\tflushing UART\n");
      // Wait for UART to end transmitting.
      while (REG_GET_FIELD(UART_STATUS_REG(0), UART_ST_UTX_OUT)) { // soc\uart_reg.h
            ;
        }
    }// end if waiting for UART to flush
       if (flushUARTAtEndOfWakeStub) {
      DBG_PRINTS_IN_WAKEUP1("\tflushing UART\n");
      // Wait for UART to end transmitting.
      while (REG_GET_FIELD(UART_STATUS_REG(0), UART_ST_UTX_OUT)) { // soc\uart_reg.h
            ;
        }
    }// end if waiting for UART to flush

    // re-enter deep sleep

    // Set the pointer of the wake stub function.
    REG_WRITE(RTC_ENTRY_ADDR_REG, (uint32_t)&esp_wake_deep_sleep);

    uint64_t ticksAtEnd = my_rtc_time_get();
    stubLastWakeTimeMicros = my_conv_rtc_time_us(ticksAtEnd - dbg_rtc_ticks_at_wake_start);
    stubCumulativeWakeTimeMicros += stubLastWakeTimeMicros;


    
    // Go to sleep.
    CLEAR_PERI_REG_MASK(RTC_CNTL_STATE0_REG, RTC_CNTL_SLEEP_EN);
    SET_PERI_REG_MASK(RTC_CNTL_STATE0_REG, RTC_CNTL_SLEEP_EN);
    
    // A few CPU cycles may be necessary for the sleep to start...
    while (true) {
        ets_delay_us(100);
        // feed the watchdog
        REG_WRITE(TIMG_WDTFEED_REG(0), 1); // in soc\timer_group_reg.h
      }
    // never reaches here.
  } // end if !shouldWake

  DBG_PRINTS_IN_WAKEUP1("Waking fully\n");

  // GOING TO WAKEUP FULLY
  // according to https://gist.github.com/rummyr/6dce1cb8c169a099805e751b7170a519
  // the esp_default_wake_deep_sleep must be called before "returning", but otherwise it may not be required?
  uint64_t ticksAtEnd = my_rtc_time_get();
  stubLastWakeTimeMicros = my_conv_rtc_time_us(ticksAtEnd - dbg_rtc_ticks_at_wake_start);
  stubCumulativeWakeTimeMicros += stubLastWakeTimeMicros;
  esp_default_wake_deep_sleep();


}


// attempt to use just rom functions in hardware\espressif\esp32\tools\sdk\include\esp32\rom\gpio.h
void RTC_IRAM_ATTR  RTCIRAM_setGPIOOutputState(int gpioNum, bool on) {
    // static RTC_RODATA_ATTR const char fmt_str[] = "setting ledState set to %d\n";
    // ets_printf(fmt_str, on);
  // from gpio_output_enable
    gpio_matrix_out(gpioNum, SIG_GPIO_OUT_IDX, false, false);
    GPIO_OUTPUT_SET(gpioNum, on);
}




/* set the deep sleep timer to wake in XX seconds
 * Uses a pre-calculated value per second 
*/
uint64_t RTC_IRAM_ATTR setDeepSleepTimerSecs(uint64_t time_in_sec) {
  uint64_t rtc_ticks_now = my_rtc_time_get();
  uint64_t rtc_count_delta = precalulated_rtc_count_delta_per_sec*time_in_sec  + precalulated_rtc_count_delta_constant_offset;
  
  DBG_PRINTS_IN_WAKEUP3("\tcalling my_rtc_sleep_set_wakeup_time with %lld + %lld\n", rtc_ticks_now , rtc_count_delta);
  my_rtc_sleep_set_wakeup_time(rtc_ticks_now + rtc_count_delta);
  return rtc_ticks_now + rtc_count_delta;
}


int RTC_IRAM_ATTR RTCIRAM_readGPIO(gpio_num_t gpioNum) {
    return GPIO_INPUT_GET(gpioNum);
}

void RTC_IRAM_ATTR my_rtc_sleep_set_wakeup_time(uint64_t t)
{
    WRITE_PERI_REG(RTC_CNTL_SLP_TIMER0_REG, t & UINT32_MAX);
    WRITE_PERI_REG(RTC_CNTL_SLP_TIMER1_REG, t >> 32);
}

RTC_IRAM_ATTR uint64_t my_conv_rtc_time_us(uint64_t ticks)
{
    const uint32_t cal = precalculated_clk_slowclk_cal_get;
    /* RTC counter result is up to 2^48, calibration factor is up to 2^24,
     * for a 32kHz clock. We need to calculate (assuming no overflow):
     *   (ticks * cal) >> RTC_CLK_CAL_FRACT
     *
     * An overflow in the (ticks * cal) multiplication would cause time to
     * wrap around after approximately 13 days, which is probably not enough
     * for some applications.
     * Therefore multiplication is split into two terms, for the lower 32-bit
     * and the upper 16-bit parts of "ticks", i.e.:
     *   ((ticks_low + 2^32 * ticks_high) * cal) >> RTC_CLK_CAL_FRACT
     */
    const uint64_t ticks_low = ticks & UINT32_MAX;
    const uint64_t ticks_high = ticks >> 32;
    return ((ticks_low * cal) >> RTC_CLK_CAL_FRACT) +
           ((ticks_high * cal) << (32 - RTC_CLK_CAL_FRACT));
}

uint64_t RTC_IRAM_ATTR my_rtc_time_get()
{
    SET_PERI_REG_MASK(RTC_CNTL_TIME_UPDATE_REG, RTC_CNTL_TIME_UPDATE);
    while (GET_PERI_REG_MASK(RTC_CNTL_TIME_UPDATE_REG, RTC_CNTL_TIME_VALID) == 0) {
        ets_delay_us(1); // might take 1 RTC slowclk period, don't flood RTC bus
    }
    SET_PERI_REG_MASK(RTC_CNTL_INT_CLR_REG, RTC_CNTL_TIME_VALID_INT_CLR);
    uint64_t t = READ_PERI_REG(RTC_CNTL_TIME0_REG);
    t |= ((uint64_t) READ_PERI_REG(RTC_CNTL_TIME1_REG)) << 32;
    return t;
}


boolean RTC_IRAM_ATTR arePinsConnected(gpio_num_t in, gpio_num_t out) {
  boolean input_and_output_connected = true;
  int inputState;

  RTCIRAM_setGPIOOutputState(out, 0);
  inputState = RTCIRAM_readGPIO(in);
  DBG_PRINTS_IN_WAKEUP2("\twith output LOW input is %d\n", inputState);

  if (inputState != 0) {
    DBG_PRINTS_IN_WAKEUP1("\tinput and output pins are NOT connected\n");
    return false;
  }

  RTCIRAM_setGPIOOutputState(out, 1);
  inputState = RTCIRAM_readGPIO(in);
  DBG_PRINTS_IN_WAKEUP2("\tWith output high, input is %d", inputState);

  if (inputState != 1) {
    DBG_PRINTS_IN_WAKEUP1("\tinput and output pins are NOT connected\n");
    return false;
  }
    
  DBG_PRINTS_IN_WAKEUP1("\tinput and output pins are connected\n");

  return true;
}

void RTC_IRAM_ATTR flushUART() {
      // Wait for UART to end transmitting.
      while (REG_GET_FIELD(UART_STATUS_REG(0), UART_ST_UTX_OUT)) { // soc\uart_reg.h
            ;
        }
}// end if waiting for UART to flush










