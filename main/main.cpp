#include <Arduino.h>
#include <FastLED.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"

// Arduino Nano ESP32 on-board RGB led
#define RED_PIN 46
#define GREEN_PIN 0
#define BLUE_PIN 45

// Pin Configuration
#define LED_DATA_PIN 18                         // Pin connected to the LED strip
#define SR_DATA_PIN 5                           // DS (Data input)
#define SR_CLOCK_PIN 6                          // SH_CP (Clock)
#define SR_LATCH_PIN 7                          // ST_CP (Latch)

#define NUM_LEDS 256                            // Adjust this to match the number of LEDs on your strip

CRGB leds[NUM_LEDS];

#define MIC_PIN ADC1_CHANNEL_0                  // ADC Channel to use
#define ADC_BIT_WIDTH 12
#define PEAK_HOLD_TIME 500                      // Peak hold time in milliseconds
#define PEAK_DECAY_SPEED 1                      // Peak decay speed (time between falling in ms)
#define SAMPLES 256                             // Must be a power of 2 (e.g., 64, 128, 256)
#define SAMPLING_FREQUENCY 10000                // Sampling frequency in Hz
#define AREF 3.3                                // Analog reference voltage

// --
// WS2812 16x16 LED Matrix Config
// --
#define LED_MATRIX_WIDTH 16
#define LED_MATRIX_HEIGHT 16
int XY[LED_MATRIX_WIDTH][LED_MATRIX_HEIGHT];

uint16_t calcXY(uint8_t x, uint8_t y) {
	if (x >= LED_MATRIX_WIDTH || y >= LED_MATRIX_HEIGHT)
		return NUM_LEDS;
	if (y & 1)
		x = LED_MATRIX_WIDTH - 1 - x;
	return x + (y * LED_MATRIX_WIDTH);
}

void mapXY() {
	for (int x = 0; x < LED_MATRIX_WIDTH; x++)
		for (int y = 0; y < LED_MATRIX_HEIGHT; y++)
			XY[x][y] = calcXY(x, y);
}

// --
// Simple 8x8 LED Matrix Config
// --
#define LSBFIRST 0
#define MSBFIRST 1

byte matrixBuffer[8] = {0};

void lil_shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t val) {
	uint8_t i;

	for (i = 0; i < 8; i++) {
		if (bitOrder == LSBFIRST)
			digitalWrite(dataPin, !!(val & (1 << i)));
		else
			digitalWrite(dataPin, !!(val & (1 << (7 - i))));

		digitalWrite(clockPin, HIGH);
		digitalWrite(clockPin, LOW);
	}
}

void lil_shift_register_test() {
	// Set ALL LEDs On
	digitalWrite(SR_LATCH_PIN, LOW);// Prepare to send data
	lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, 0b00000000);
	lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, 0b11111111);
	digitalWrite(SR_LATCH_PIN, HIGH);// Latch the data to output
	vTaskDelay(500 / portTICK_PERIOD_MS);

	// Step through every led one by one
	for (int row = 1; row <= 8; row++) {
		for (int col = 1; col <= 8; col++) {
			digitalWrite(SR_LATCH_PIN, LOW);// Prepare to send data
			lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, ~(1 << (col - 1)));
			lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, (1 << (row - 1)));
			digitalWrite(SR_LATCH_PIN, HIGH);// Latch the data to output
			vTaskDelay(50 / portTICK_PERIOD_MS);
		}
	}

	// Set ALL LEDs Off
	digitalWrite(SR_LATCH_PIN, LOW);// Prepare to send data
	lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, 0b11111111);
	lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, 0b00000000);
	digitalWrite(SR_LATCH_PIN, HIGH);// Latch the data to output
}

void refreshMatrix() {
	for (int row = 0; row < 8; row++) {
		digitalWrite(SR_LATCH_PIN, LOW);
		lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, LSBFIRST, ~matrixBuffer[row]);// Columns
		lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, (1 << row));        // Row
		digitalWrite(SR_LATCH_PIN, HIGH);
	}
}

// --
// Use ESP's Continuous ADC sampling into DMA for efficient high sampling rate
// Process audio samples into FFT frequency domain data
// --
static uint8_t audioBuffer[SAMPLES];
double vReal[SAMPLES];
double vImag[SAMPLES];

void adc_dma_task(void *arg) {
	adc_continuous_handle_t adc_handle = (adc_continuous_handle_t) arg;
	uint32_t read_bytes = 0;

	while (1) {
		esp_err_t ret = adc_continuous_read(adc_handle, audioBuffer, SAMPLES, &read_bytes, portMAX_DELAY);
		if (ret == ESP_OK) {
			ESP_LOGI("ADC_TASK", "Read %lu bytes", (unsigned long) read_bytes);
			// Process buffer here (e.g., convert to voltage, perform FFT, etc.)
		} else {
			ESP_LOGE("ADC_TASK", "ADC read failed");
		}
	}
}

// --
// Initialize
// --

void lil_init_led() {
    // Initialize onboard LED
	pinMode(RED_PIN, OUTPUT);
	pinMode(GREEN_PIN, OUTPUT);
	pinMode(BLUE_PIN, OUTPUT);
	analogWrite(RED_PIN, 0);    // 50% brightness
	analogWrite(GREEN_PIN, 255);// Off
	analogWrite(BLUE_PIN, 128); // 50% brightness

	// Initialize FastLED and set brightness
	FastLED.addLeds<WS2812, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
	FastLED.setBrightness(10);// Adjust as necessary
	FastLED.setTemperature(Candle);

    // Test LED Matrix with rainbow
	for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV(map(i, 0, NUM_LEDS - 1, 0, 255), 255, 255);
	FastLED.show();
	vTaskDelay(1000 / portTICK_PERIOD_MS);
	FastLED.clear();
	FastLED.show();

	// Initialize 8x8 and test all lights
	pinMode(SR_DATA_PIN, OUTPUT);
	pinMode(SR_CLOCK_PIN, OUTPUT);
	pinMode(SR_LATCH_PIN, OUTPUT);
    lil_shift_register_test();

    // Turn onboard LED off
	analogWrite(RED_PIN, 255);
	analogWrite(GREEN_PIN, 255);
	analogWrite(BLUE_PIN, 255);
}

static TaskHandle_t app_main_task_handle;
static bool IRAM_ATTR adc_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data) {
	BaseType_t mustYield = pdFALSE;
	//Notify that ADC continuous driver has done enough number of conversions
	vTaskNotifyGiveFromISR(app_main_task_handle, &mustYield);
	return (mustYield == pdTRUE);
}

extern "C" void app_main(void) {
    initArduino();

    printf("Internal Total heap %d, internal Free Heap %d\n", (int)ESP.getHeapSize(), (int)ESP.getFreeHeap());
    printf("SPIRam Total heap %d, SPIRam Free Heap %d\n", (int)ESP.getPsramSize(), (int)ESP.getFreePsram());
    printf("ChipRevision %d, Cpu Freq %d, SDK Version %s\n", (int)ESP.getChipRevision(), (int)ESP.getCpuFreqMHz(), ESP.getSdkVersion());
    printf("Flash Size %d, Flash Speed %d\n", (int)ESP.getFlashChipSize(), (int)ESP.getFlashChipSpeed());

    lil_init_led();
    mapXY();

	app_main_task_handle = xTaskGetCurrentTaskHandle();

	// ADC configuration
	adc_continuous_handle_t adc_handle = NULL;
	adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 1024,
        .conv_frame_size = SAMPLES
	};
	ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

	adc_continuous_config_t dig_cfg = {
        .pattern_num = 1,
        .adc_pattern = NULL,
        .sample_freq_hz = SAMPLING_FREQUENCY,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2
	};
	adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {
			{
                .atten = ADC_ATTEN_DB_12,
                .channel = ADC_CHANNEL_0,
                .unit = ADC_UNIT_1,
                .bit_width = ADC_BIT_WIDTH,
			}
	};
	dig_cfg.pattern_num = 1;
	dig_cfg.adc_pattern = adc_pattern;
	ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &dig_cfg));

	adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = adc_conv_done_cb,
        .on_pool_ovf = NULL
	};
	ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_handle, &cbs, NULL));

	ESP_ERROR_CHECK(adc_continuous_start(adc_handle));

	// Create the task to process ADC data
	//xTaskCreate(adc_dma_task, "adc_dma_task", 4096, adc_handle, 5, NULL);

	uint32_t read_bytes = 0;
	esp_err_t ret;

    while (1) {
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		while (1) {
			ESP_LOGI("SoundFlowerSampling", "entering main loop");
			ret = adc_continuous_read(adc_handle, audioBuffer, SAMPLES, &read_bytes, 0);
			if (ret == ESP_OK) {
				ESP_LOGI("SoundFlowerSampling", "ret is %x, ret_num is %" PRIu32 " bytes", ret, read_bytes);
				printf("Captured sample: ");
				for (int i = 0; i < SAMPLES; i++) {
					printf("%i ", audioBuffer[i]);
				}
				printf("\n");
				/**
                 * Because printing is slow, so every time you call `ulTaskNotifyTake`, it will immediately return.
                 * To avoid a task watchdog timeout, add a delay here. When you replace the way you process the data,
                 * usually you don't need this delay (as this task will block for a while).
                 */
				vTaskDelay(1);
			} else if (ret == ESP_ERR_TIMEOUT) {
				//We try to read `EXAMPLE_READ_LEN` until API returns timeout, which means there's no available data
				ESP_LOGE("SoundFlowerSampling", "Got error timeout");
				break;
			}
		}
	}

	ESP_ERROR_CHECK(adc_continuous_stop(adc_handle));
	ESP_ERROR_CHECK(adc_continuous_deinit(adc_handle));
}
