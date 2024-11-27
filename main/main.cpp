#include <Arduino.h>
#include <FastLED.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_adc/adc_continuous.h>
#include <esp_log.h>
#include <esp_dsp.h>
#include <esp_timer.h>

// Arduino Nano ESP32 on-board RGB led
#define RED_PIN 46
#define GREEN_PIN 0
#define BLUE_PIN 45

// Pin Configuration
#define LED_DATA_PIN 18        // Pin connected to the LED strip
#define SR_DATA_PIN GPIO_NUM_5 // DS (Data input)
#define SR_CLOCK_PIN GPIO_NUM_6// SH_CP (Clock)
#define SR_LATCH_PIN GPIO_NUM_7// ST_CP (Latch)

// WS2812 16x16 LED Matrix Display Configuration
#define NUM_LEDS 256// Adjust this to match the number of LEDs on your strip
#define LED_NUM_BANDS 8
#define LED_BAND_HOLD_TIME 75
#define LED_PEAK_HOLD_TIME 500// Peak hold time in milliseconds
#define LED_PEAK_DECAY_SPEED 50// Peak decay speed (time between falling in ms)
CRGB leds[NUM_LEDS];

// Shift Register 8x8 LED Matrix Configuration
#define SR_LED_MATRIX_REFRESH_RATE 50//in microseconds

// Audio Capture Configuration
#define MIC_PIN ADC1_CHANNEL_0// ADC Channel to use
#define ADC_BIT_WIDTH 12
#define SAMPLES 1024            // Must be a power of 2 (e.g., 64, 128, 256)
#define SAMPLING_FREQUENCY 48000// Sampling frequency in Hz
#define AREF 3.3                // Analog reference voltage

// --
// WS2812 16x16 Serpentine LED Matrix
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
	//FastLED.setTemperature(Candle);

	// Test LED Matrix with rainbow
	// for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV(map(i, 0, NUM_LEDS - 1, 0, 255), 255, 255);
	// FastLED.show();
	// vTaskDelay(1000 / portTICK_PERIOD_MS);
	// FastLED.clear();
	// FastLED.show();

	// Turn onboard LED off (We're done)
	analogWrite(RED_PIN, 255);
	analogWrite(GREEN_PIN, 255);
	analogWrite(BLUE_PIN, 255);
}

void updateLedMatrixGrouped8(float *fft) {
	static bool initializing = true;

	struct lil_vu_band {
		int min;//minimum
		int max;
		float_t scaling_coeff;
		int64_t hold_timer;//microseconds since last reset
		int value;
		int peak;
		int64_t peak_hold_timer;//microseconds since last reset
	};
	static struct lil_vu_band lilVUBands[8];

	if (initializing) {
		//8 bands, 12kHz top band
		//derived from https://github.com/s-marley/ESP32_FFT_VU/blob/master/FFT.xlsx
		//TODO: write this in code and not an excel?
		lilVUBands[0] = {.min = 0, .max = 3, .scaling_coeff = 0.20, .hold_timer = 0, .value = 0, .peak = 0, .peak_hold_timer = 0};
		lilVUBands[1] = {.min = 4, .max = 5, .scaling_coeff = 0.20, .hold_timer = 0, .value = 0, .peak = 0, .peak_hold_timer = 0};
		lilVUBands[2] = {.min = 6, .max = 11, .scaling_coeff = 0.20, .hold_timer = 0, .value = 0, .peak = 0, .peak_hold_timer = 0};
		lilVUBands[3] = {.min = 12, .max = 22, .scaling_coeff = 0.20, .hold_timer = 0, .value = 0, .peak = 0, .peak_hold_timer = 0};
		lilVUBands[4] = {.min = 23, .max = 46, .scaling_coeff = 0.20, .hold_timer = 0, .value = 0, .peak = 0, .peak_hold_timer = 0};
		lilVUBands[5] = {.min = 47, .max = 93, .scaling_coeff = 0.20, .hold_timer = 0, .value = 0, .peak = 0, .peak_hold_timer = 0};
		lilVUBands[6] = {.min = 94, .max = 191, .scaling_coeff = 0.20, .hold_timer = 0, .value = 0, .peak = 0, .peak_hold_timer = 0}; 
		lilVUBands[7] = {.min = 192, .max = 511, .scaling_coeff = 0.20, .hold_timer = 0, .value = 0, .peak = 0, .peak_hold_timer = 0};

		initializing = false;
	}

	bool timerExpired;
	bool largerVal;

	for (int i = 1; i < SAMPLES / 2; i++) {
		for (int bnd_i = 0; bnd_i < LED_NUM_BANDS; bnd_i++) {
			if (i >= lilVUBands[bnd_i].min && i <= lilVUBands[bnd_i].max) {
				//elasped time since band update
				int64_t elapsed_time_ms = (esp_timer_get_time() - lilVUBands[bnd_i].hold_timer) / 1000;

				//scale the real part of fft by the band's scaling coeff
				int new_val = (int) fft[i * 2] * lilVUBands[bnd_i].scaling_coeff;

				//conditions to update band
				timerExpired = (elapsed_time_ms > LED_BAND_HOLD_TIME);
				largerVal = (new_val > (int) lilVUBands[bnd_i].value);

				if (timerExpired || largerVal) {
					// if (timerExpired) ESP_LOGI("updateLedMatrixGrouped8", "expired %d elapsed %" PRId64, timerExpired, elapsed_time_ms);
					// if (largerVal) ESP_LOGI("updateLedMatrixGrouped8", "this %d larger than %d", (int) vReal[i], lilVUBands[bnd_i].value);
					lilVUBands[bnd_i].value = new_val;
					lilVUBands[bnd_i].hold_timer = esp_timer_get_time();
				}
			}
		}
	}

	for (int x = 0; x < LED_NUM_BANDS; x++) {
		//enforce the bounds
		if (lilVUBands[x].value > LED_MATRIX_HEIGHT) lilVUBands[x].value = LED_MATRIX_HEIGHT;
		if (lilVUBands[x].value < 0) lilVUBands[x].value = 0;

		if (lilVUBands[x].value > lilVUBands[x].peak) {
			lilVUBands[x].peak = lilVUBands[x].value;
			lilVUBands[x].peak_hold_timer = esp_timer_get_time();
		} else if ((esp_timer_get_time() - lilVUBands[x].peak_hold_timer) / 1000 > LED_PEAK_HOLD_TIME) {
			//decay the peak
			if (lilVUBands[x].peak >= 1) lilVUBands[x].peak--;
		}

		// Set the matrix column based on the amplitude
		for (int y = 0; y < LED_MATRIX_HEIGHT; y++) {
			leds[XY[x * 2][y]] = (y < lilVUBands[x].value) ? CHSV(map(y, 0, LED_MATRIX_HEIGHT - 1, 0, 255), 255, 255) : CHSV(0, 0, 0);
			leds[XY[x * 2 + 1][y]] = (y < lilVUBands[x].value) ? CHSV(map(y, 0, LED_MATRIX_HEIGHT - 1, 0, 255), 255, 255) : CHSV(0, 0, 0);
		}

		// Set the peak pixel(s)
		if (lilVUBands[x].peak > 1) {
			leds[XY[x * 2][lilVUBands[x].peak - 1]] = CRGB::White;
			leds[XY[x * 2 + 1][lilVUBands[x].peak - 1]] = CRGB::White;
		}
	}

	FastLED.show();// Update LED strip
}

// --
// Simple 8x8 LED Matrix on 2 8-bit Shift Registers
// --
#define LSBFIRST 0
#define MSBFIRST 1

volatile bool matrixBufferReady = false;
static byte matrixBuffer[8] = {0};
static esp_timer_handle_t refreshMatrixTimerHandle = NULL;
static TaskHandle_t refreshMatrixTaskHandle = NULL;

void lil_shiftOut(gpio_num_t dataPin, gpio_num_t clockPin, uint8_t bitOrder, uint8_t val) {
	for (uint8_t i = 0; i < 8; i++) {
		int bit = (bitOrder == LSBFIRST) ? !!(val & (1 << i)) : !!(val & (1 << (7 - i)));
		// Set data pin
		gpio_set_level(dataPin, bit);
		// Pulse clock pin
		gpio_set_level(clockPin, 1);
		gpio_set_level(clockPin, 0);
	}
}

void refreshMatrix() {
	static byte local_matrixBuffer[8] = {0};
	if (matrixBufferReady) {
		memcpy8(local_matrixBuffer, matrixBuffer, 8);
		matrixBufferReady = false;
	}
	for (int row = 0; row < 8; row++) {
		gpio_set_level(SR_LATCH_PIN, 0);
		lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, LSBFIRST, ~local_matrixBuffer[row]);// Columns
		lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, (1 << row));        // Row
		gpio_set_level(SR_LATCH_PIN, 1);
	}
}

void refreshMatrixTimer(void *param) {
	if (refreshMatrixTaskHandle != NULL) {
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		vTaskNotifyGiveFromISR(refreshMatrixTaskHandle, &xHigherPriorityTaskWoken);
		// Yield if necessary
		if (xHigherPriorityTaskWoken == pdTRUE) {
			portYIELD_FROM_ISR();
		}
	}
}

void refreshMatrixTask(void *param) {
	while (1) {
		// Wait for notification from the timer
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		// Call refreshMatrix
		refreshMatrix();
	}
}

void lil_shift_register_test() {
	// Set ALL LEDs On
	gpio_set_level(SR_LATCH_PIN, 0);// Prepare to send data
	lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, 0b00000000);
	lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, 0b11111111);
	gpio_set_level(SR_LATCH_PIN, 1);// Latch the data to output
	vTaskDelay(500 / portTICK_PERIOD_MS);

	// Step through every LED one-by-one
	for (int row = 1; row <= 8; row++) {
		for (int col = 1; col <= 8; col++) {
			gpio_set_level(SR_LATCH_PIN, 0);// Prepare to send data
			lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, ~(1 << (col - 1)));
			lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, (1 << (row - 1)));
			gpio_set_level(SR_LATCH_PIN, 1);// Latch the data to output
			vTaskDelay(25 / portTICK_PERIOD_MS);
		}
	}

	// Set ALL LEDs Off
	gpio_set_level(SR_LATCH_PIN, 0);// Prepare to send data
	lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, 0b11111111);
	lil_shiftOut(SR_DATA_PIN, SR_CLOCK_PIN, MSBFIRST, 0b00000000);
	gpio_set_level(SR_LATCH_PIN, 1);// Latch the data to output
}

void lil_init_8x8() {
	// Initialize 8x8 and test all lights
	gpio_config_t io_conf = {};
	io_conf.intr_type = GPIO_INTR_DISABLE;// No interrupts
	io_conf.mode = GPIO_MODE_OUTPUT;      // Set as output
	io_conf.pin_bit_mask = (1ULL << SR_LATCH_PIN) | (1ULL << SR_DATA_PIN) | (1ULL << SR_CLOCK_PIN);
	io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_config(&io_conf);
	lil_shift_register_test();

	// Set up refresh task for multiplexing
	const esp_timer_create_args_t timer_args = {
			.callback = &refreshMatrixTimer,  // Function to call
			.arg = NULL,                      // Arguments to pass (optional)
			.dispatch_method = ESP_TIMER_TASK,// Runs callback in a dedicated task
			.name = "refreshMatrixTimer",     // Timer name
			.skip_unhandled_events = false};
	ESP_ERROR_CHECK(esp_timer_create(&timer_args, &refreshMatrixTimerHandle));
	ESP_ERROR_CHECK(esp_timer_start_periodic(refreshMatrixTimerHandle, SR_LED_MATRIX_REFRESH_RATE));
	//testMatrixBuffer();
	// Create the matrix refresh task pinned to Cpu core 1
	xTaskCreatePinnedToCore(refreshMatrixTask, "refreshMatrixTask", 2048, NULL, 5, &refreshMatrixTaskHandle, 1);
}

// void testMatrixBuffer() {
// 	for (int row = 0; row < 8; row++) {
// 		matrixBuffer[row] = (1 << (row));
// 	}
// 	matrixUpdateReady = true;
// }

void updateMatrixBuffer2(float *fft) {
	static bool initializing = true;

	struct lil_vu_band {
		int min;
		int max;
		float scaling_coeff;
		int value;
		int peak;
		int64_t band_hold_timer;// Microseconds since last reset
		int64_t peak_hold_timer;// Microseconds since last peak reset
		int64_t peak_decay_timer;
	};
	static lil_vu_band lilVUBands[8];

	if (initializing) {
		lilVUBands[0] = {.min = 1, .max = 3, .scaling_coeff = 0.10, .value = 0, .peak = 0, .band_hold_timer = 0, .peak_hold_timer = 0, .peak_decay_timer = 0};
		lilVUBands[1] = {.min = 4, .max = 5, .scaling_coeff = 0.10, .value = 0, .peak = 0, .band_hold_timer = 0, .peak_hold_timer = 0, .peak_decay_timer = 0};
		lilVUBands[2] = {.min = 6, .max = 11, .scaling_coeff = 0.10, .value = 0, .peak = 0, .band_hold_timer = 0, .peak_hold_timer = 0, .peak_decay_timer = 0};
		lilVUBands[3] = {.min = 12, .max = 22, .scaling_coeff = 0.10, .value = 0, .peak = 0, .band_hold_timer = 0, .peak_hold_timer = 0, .peak_decay_timer = 0};
		lilVUBands[4] = {.min = 23, .max = 46, .scaling_coeff = 0.10, .value = 0, .peak = 0, .band_hold_timer = 0, .peak_hold_timer = 0, .peak_decay_timer = 0};
		lilVUBands[5] = {.min = 47, .max = 93, .scaling_coeff = 0.10, .value = 0, .peak = 0, .band_hold_timer = 0, .peak_hold_timer = 0, .peak_decay_timer = 0};
		lilVUBands[6] = {.min = 94, .max = 191, .scaling_coeff = 0.10, .value = 0, .peak = 0, .band_hold_timer = 0, .peak_hold_timer = 0, .peak_decay_timer = 0};
		lilVUBands[7] = {.min = 192, .max = 511, .scaling_coeff = 0.10, .value = 0, .peak = 0, .band_hold_timer = 0, .peak_hold_timer = 0, .peak_decay_timer = 0};

		initializing = false;
	}

	for (int row = 0; row < 8; row++) {
		float sum = 0;

		// Sum FFT values for the band's frequency range
		for (int i = lilVUBands[row].min; i <= lilVUBands[row].max; i++) {
			sum += (fft[i * 2] / 2) * lilVUBands[row].scaling_coeff;
		}

		// Calculate the new amplitude
		int new_value = (int) sum;
		if (new_value > 8) new_value = 8;
		if (new_value < 0) new_value = 0;

		// Check if the band hold timer has expired
		int64_t elapsed_band_time_ms = (esp_timer_get_time() - lilVUBands[row].band_hold_timer) / 1000;
		bool timer_expired = (elapsed_band_time_ms > LED_BAND_HOLD_TIME);
		bool larger_value = (new_value > lilVUBands[row].value);

		// Update the band's value if the timer expired or the new value is larger
		if (timer_expired || larger_value) {
			lilVUBands[row].value = new_value;
			lilVUBands[row].band_hold_timer = esp_timer_get_time();// Reset the band hold timer
		}

		// Update the peak
		if (lilVUBands[row].value > lilVUBands[row].peak) {
			lilVUBands[row].peak = lilVUBands[row].value;
			lilVUBands[row].peak_hold_timer = esp_timer_get_time();// Reset the peak hold timer
		} else if (((esp_timer_get_time() - lilVUBands[row].peak_hold_timer) / 1000) > LED_PEAK_HOLD_TIME) {
			// Decay the peak based on a timer
			if (((esp_timer_get_time() - lilVUBands[row].peak_decay_timer) / 1000) > LED_PEAK_DECAY_SPEED) {
				if (lilVUBands[row].peak > 0) {
					lilVUBands[row].peak--;
					lilVUBands[row].peak_decay_timer = esp_timer_get_time();// Reset the peak hold timer
				}
			}
		}

		// Update the matrix buffer based on the band's value
		matrixBuffer[row] = (1 << lilVUBands[row].value) - 1;

		// Optionally add the peak indicator
		if (lilVUBands[row].peak > 1) {
			matrixBuffer[row] |= (1 << (lilVUBands[row].peak - 1));
		}
	}

	matrixBufferReady = true;
}


// --
// Initialize
// --

// --
// Use ESP's Continuous ADC sampling into DMA for efficient high sampling rate
// Process audio samples into FFT frequency domain data
// --
__attribute__((aligned(4))) static uint8_t audioBuffer[SAMPLES * SOC_ADC_DIGI_RESULT_BYTES];
float vFFT[SAMPLES * 2];// FFT Vector. SAMPLES * 2 because we need space for both real and imaginary parts of each sample

static TaskHandle_t app_main_task_handle;
static bool IRAM_ATTR adc_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data) {
	BaseType_t mustYield = pdFALSE;
	vTaskNotifyGiveFromISR(app_main_task_handle, &mustYield);
	return (mustYield == pdTRUE);
}
adc_continuous_handle_t lil_adc_handle = NULL;

void lil_init_adc() {
	adc_continuous_handle_cfg_t adc_config = {
			.max_store_buf_size = SAMPLES * SOC_ADC_DIGI_RESULT_BYTES * 4,
			.conv_frame_size = SAMPLES * SOC_ADC_DIGI_RESULT_BYTES};
	ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &lil_adc_handle));

	adc_continuous_config_t dig_cfg = {
			.pattern_num = 1,
			.adc_pattern = NULL,
			.sample_freq_hz = SAMPLING_FREQUENCY,
			.conv_mode = ADC_CONV_SINGLE_UNIT_1,
			.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2};
	adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {
			{
					.atten = ADC_ATTEN_DB_12,
					.channel = ADC_CHANNEL_0,
					.unit = ADC_UNIT_1,
					.bit_width = ADC_BIT_WIDTH,
			}};
	dig_cfg.pattern_num = 1;
	dig_cfg.adc_pattern = adc_pattern;
	ESP_ERROR_CHECK(adc_continuous_config(lil_adc_handle, &dig_cfg));

	adc_continuous_evt_cbs_t cbs = {
			.on_conv_done = adc_conv_done_cb,
			.on_pool_ovf = NULL};
	ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(lil_adc_handle, &cbs, NULL));

	ESP_ERROR_CHECK(adc_continuous_start(lil_adc_handle));
}


extern "C" void app_main(void) {
	ESP_LOGI("app_main", "Started. Running on core: %d", xPortGetCoreID());
	initArduino();

	printf("Internal Total heap %d, internal Free Heap %d\n", (int) ESP.getHeapSize(), (int) ESP.getFreeHeap());
	printf("SPIRam Total heap %d, SPIRam Free Heap %d\n", (int) ESP.getPsramSize(), (int) ESP.getFreePsram());
	printf("ChipRevision %d, Cpu Freq %d, SDK Version %s\n", (int) ESP.getChipRevision(), (int) ESP.getCpuFreqMHz(), ESP.getSdkVersion());
	printf("Flash Size %d, Flash Speed %d\n", (int) ESP.getFlashChipSize(), (int) ESP.getFlashChipSpeed());

	ESP_ERROR_CHECK(dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE));

	app_main_task_handle = xTaskGetCurrentTaskHandle();

	lil_init_8x8();
	lil_init_led();
	lil_init_adc();
	mapXY();

	uint32_t read_bytes = 0;
	esp_err_t ret;
	uint16_t sampleIndex = 0;

	while (1) {
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		while (1) {
			//ESP_LOGI("SoundFlowerSampling", "entering main loop");

			ret = adc_continuous_read(lil_adc_handle, audioBuffer, SAMPLES * SOC_ADC_DIGI_RESULT_BYTES, &read_bytes, 0);

			if (ret == ESP_OK) {
				//ESP_LOGI("SoundFlowerSampling", "ret is %x, ret_num is %" PRIu32 " bytes", ret, read_bytes);
				sampleIndex = 0;

				for (int i = 0; i < read_bytes; i += SOC_ADC_DIGI_RESULT_BYTES) {
					adc_digi_output_data_t *p = (adc_digi_output_data_t *) &audioBuffer[i];
					uint32_t data = p->type2.data;
					vFFT[sampleIndex * 2] = (float) data / 4096.0 * AREF;//real part
					vFFT[sampleIndex * 2 + 1] = 0.0;                     //imaginary part
					sampleIndex++;
				}

				// Perform FFT
				dsps_fft2r_fc32(vFFT, SAMPLES);

				// Perform bit reversal
				dsps_bit_rev_fc32(vFFT, SAMPLES);

				// Convert complex values to magnitudes
				dsps_cplx2reC_fc32(vFFT, SAMPLES);

				// Perform visualizations
				updateLedMatrixGrouped8(vFFT);
				updateMatrixBuffer2(vFFT);

				//vTaskDelay(50 / portTICK_PERIOD_MS);

			} else if (ret == ESP_ERR_TIMEOUT) {
				//No available data
				break;
			}
		}
	}

	ESP_ERROR_CHECK(adc_continuous_stop(lil_adc_handle));
	ESP_ERROR_CHECK(adc_continuous_deinit(lil_adc_handle));
}
