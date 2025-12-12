# Inverter Monitoring System

A comprehensive embedded systems project for monitoring power inverter and battery status in real-time using an ESP32 microcontroller with a graphical user interface (GUI).

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Project Architecture](#project-architecture)
- [Component Details](#component-details)
- [Installation & Setup](#installation--setup)
- [Building the Project](#building-the-project)
- [Usage](#usage)
- [Configuration](#configuration)
- [Performance Metrics](#performance-metrics)
- [Development](#development)
- [Troubleshooting](#troubleshooting)
- [License](#license)
- [Contributing](#contributing)

## Overview

The Inverter Monitoring System is an embedded firmware application designed to monitor and display real-time power metrics from a DC-AC inverter and battery system. The system continuously tracks electrical parameters, environmental conditions, and system status, presenting this information through a user-friendly graphical interface on an ST7735 128×160 pixel LCD display.

This project leverages the ESP-IDF (Espressif IoT Development Framework) and implements a multi-threaded architecture using FreeRTOS for optimal performance and responsiveness.

## Features

### Core Monitoring Capabilities
- **Power Monitoring**: Real-time measurement of:
  - Current consumption (in Amperes)
  - Voltage levels (in Volts)
  - Apparent power calculation (in VA - Volt-Amperes)
  - Timestamped data logging

- **Environmental Monitoring**: 
  - Temperature sensing via AHT20 sensor
  - Relative humidity measurement
  - Automatic sensor readout at configurable intervals

- **Inverter Status Tracking**:
  - Active/Idle state monitoring
  - Battery status indicators (Charging/Discharging/Idle)
  - System health status and alerts

### User Interface
- **Graphical Display**: ST7735 128×160 pixel color LCD with LVGL (Light and Versatile Graphics Library)
- **Multi-Screen Navigation**: Multiple information screens accessible via navigation buttons
- **Real-Time Updates**: Dynamic UI refresh with live data visualization
- **Alert System**: Visual and informational alerts for system events

### System Architecture
- **Multi-Core Processing**: Leverages both ESP32 cores for parallel task execution
- **Real-Time Guarantees**: Task-based scheduling with configurable priorities
- **Thread Safe Operations**: Mutex protected shared resource access
- **Watchdog Protection**: Task watchdog monitoring for system reliability

## Hardware Requirements

### Microcontroller
- **ESP32** (with dual cores recommended)

### Sensors & Input Devices
- **AHT20 Digital Humidity and Temperature Sensor**
  - I2C interface
  - Operating temperature: -40°C to +85°C
  - Operating humidity: 0% to 100% RH

- **Analog Current Sensor (ACS712-25A Sensor)**
  - ADC output
  - GPIO33 input

- **Analog Voltage Sensor**
  - ADC output
  - GPIO32 input

### Display
- **ST7735 128×160 Pixel Color LCD**
  - SPI interface
  - 65K color palette
  - RGB565 format

### User Input
- **Navigation Button 1** (GPIO25) - Previous/Back
- **Navigation Button 2** (GPIO26) - Next/Forward

### Connectivity
- **I2C Bus**: SDA (GPIO14), SCL (GPIO27) for AHT20 sensor
- **SPI Bus**: MOSI (GPIO18), SCLK (GPIO5) for LCD display
- **Control Pins**: CS (GPIO22), DC (GPIO19), RST (GPIO21) for LCD

## Project Architecture

### Multi-Task Design

The system implements a multi-task architecture with FreeRTOS, distributing workload across different priorities and processor cores:

```
Core 0 (Processing)          Core 1 (UI Rendering)
├── ADC Read Task            ├── Display Task
│   (Priority: 5)            │   (Priority: 5)
├── AHT Sensor Task          └── LVGL Handler Task
│   (Priority: 4)                (Priority: 4)
└── Calculation Task
    (Priority: 3)
```

### Task Descriptions

| Task | Priority | Core | Stack Size | Period | Purpose |
|------|----------|------|-----------|--------|---------|
| ADC Read | 5 | 0 | 3072 B | 20 ms | Read and buffer power measurements |
| AHT Sensor | 4 | 0 | 3072 B | 1600 ms | Read temperature and humidity |
| Calculation | 3 | 0 | 4096 B | N/A | Compute statistics from raw data |
| Display | 5 | 1 | 8192 B | 33 ms | Manage LCD output |
| LVGL Handler | 4 | 1 | 8192 B | 33 ms | Process UI events and rendering |

### Data Flow

```
Sensors (ADC, AHT20)
    ↓
Dedicated Read Tasks
    ↓
Input Queues (aht_queue, power_queue)
    ↓
Calculation Task
    ↓
Output Queue (final_data_queue)
    ↓
Display Task & UI Update
    ↓
LCD Display
```

### Synchronization Mechanisms

- **Queues**: FreeRTOS queues for inter-task communication and data buffering
- **Mutexes**: LVGL display mutex ensures thread safe graphics rendering
- **Task Watchdog**: Monitors tasks' health and triggers system reset on timeout

## Component Details

### Power Monitoring (`components/power`)

**Files**: `power_monitor.hpp`, `power_monitor.cpp`

Handles continuous ADC sampling and power metric calculations:

```cpp
namespace adc {
    struct data_t {
        float current_avg;       // Average current in Amperes
        float voltage_avg;       // Average voltage in Volts
        float apparent_power;    // Apparent power in VA
        uint32_t timestamp_ms;   // Measurement timestamp
        bool valid;              // Data validity flag
    };
    
    class driver {
        // ADC initialization and configuration
        // Continuous sampling management
        // Data validation and filtering
    };
}
```

**Key Features**:
- Continuous ADC mode for uninterrupted sampling
- Analog calibration for more accurate measurements
- Uses a background task for sampling and updating measurements after receiving a notification from ISR 
- Moving average filtering
- Data validity checking

### Temperature and Humidity Monitoring of the Batteries (`components/aht`)

**Files**: `aht20.h`, `aht20.c`

Interfaces with the AHT20 temperature and humidity sensor via I2C:

```cpp
typedef struct {
    float temperature;   // Celsius
    float humidity;      // Relative humidity
} aht20_data_t;
```

**Key Features**:
- I2C protocol implementation
- Configurable measurement intervals
- Temperature range: -40°C to +85°C
- Humidity range: 0% to 100% RH

### Display Management (`components/display`)

**Files**: `display.hpp`, `display.cpp`

Manages the LCD and LVGL graphics framework:

```cpp
namespace display {
    esp_err_t init(void);
    void deinit(void);
    void bootup_screen(void);
    void create_ui(void);
    void update_data(const sys::data_t& data);
    void next_screen(void);
    void prev_screen(void);
}
```

**Subcomponents**:
- **Screens** (`display/screens`): Multiple information pages
- **Alerts** (`display/alert`): Alert notification system
- **Utilities** (`display/utils`): Colors, and assets

### Alert System (`components/alert`)

**Files**: `alert.hpp`, `alert.cpp`

Provides real-time monitoring and alerting for critical system parameters:

```cpp
namespace display {
    class alert_subsystem_t {
    private:
        enum class voltage_t : int8_t {
            TOO_LOW = -2, LOW = -1, OK = 0, HIGH = 1
        };
        enum class current_t : int8_t {
            CHARGE_TOO_HIGH = -2, CHARGE_HIGH = -1, OK = 0, HIGH = 1, TOO_HIGH = 2
        };
        enum class temp_t : int8_t {
            TOO_LOW = -2, LOW = -1, OK = 0, HIGH = 1, TOO_HIGH = 2
        };
        enum class hmdt_t : int8_t {
            TOO_LOW = -2, LOW = -1, OK = 0, HIGH = 1, TOO_HIGH = 2
        };
        enum class batt_t : uint8_t {
            OK = 0, BELOW_50, BELOW_15, BELOW_10, BELOW_5
        };
    };
}
```

**Alert Thresholds**:
- **Voltage**: Too Low (<9.0V), Low (9.0-10.5V), High (>12.6V)
- **Current**: Charge Too High (<-15A), High Discharge (>20A), Too High (>25A)
- **Temperature**: Too Low (<0°C), Low (0-10°C), High (45-60°C), Too High (>60°C)
- **Humidity**: Too Low (<10%), Low (10-20%), High (70-80%), Too High (>80%)
- **Battery Level**: Below 50%, 15%, 10%, 5%

**Key Features**:
- Continuous parameter monitoring during display updates
- Classification of alert severity levels
- Popup notification system (LVGL implemented, UI pending)
- Thread-safe integration with display task

### Button Input Handler (`components/button`)

**Files**: `button_handler.hpp`, `button_handler.cpp`

Manages GPIO button input with debouncing and long press detection:

```cpp
enum class event_t : uint8_t {
    NO_EVENT = 0,
    NEXT_BUTTON_PRESSED,
    PREV_BUTTON_PRESSED,
    NEXT_LONG_PRESSED,
    PREV_LONG_PRESSED
};
```

**Features**:
- 50ms debounce delay
- ISR based event detection
- Long press detection (2 seconds (configurable))
- Thread safe event queue
- LED brightness control via PWM and the ESP32's ledc periphal

### System Utilities (`components/system`)

**Files**: `system.hpp`, `system.cpp`

Core data structures and system state management:

```cpp
namespace sys {
    enum class inv_status_t { IDLE = 0, ACTIVE };
    enum class batt_status_t { IDLE = 0, DISCHARGING, RECHARGING };
    
    struct data_t {
        float battery_voltage;
        float current;
        float voltage;
        float apparent_power;
        float temperature;
        float humidity;
        inv_status_t inverter_status;
        batt_status_t battery_status;
        // Additional system metrics...
    };
}
```

### ST7735 Display Driver (`components/st7735`)

**Files**: `st7735.h`, `st7735.c`

Low-level SPI driver for the ST7735 LCD controller with support for:
- SPI communication configuration
- RGB565 color format
- Hardware reset and data/command signaling

### Configuration (`components/config`)

**Files**: `config.hpp`

Centralized configuration for all system parameters:

```cpp
namespace config {
    // Task configuration
    constexpr uint16_t CALC_TASK_STACK_SIZE = 4096;
    constexpr uint16_t CALC_TASK_PRIORITY = 3;
    
    // Pin definitions
    constexpr gpio_num_t AHT_SDA_PIN = GPIO_NUM_14;
    constexpr gpio_num_t CURRENT_SENSOR_PIN = GPIO_NUM_33;
    
    // Display settings
    constexpr uint16_t LCD_WIDTH = 128;
    constexpr uint16_t LCD_HEIGHT = 160;
}
```

## Installation & Setup

### Prerequisites

1. **ESP-IDF**: Version 5.0 or later
   ```bash
   # Install ESP-IDF (if not already installed)
   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.bat  # On Windows
   ```

2. **Python Dependencies**:
   ```bash
   pip install -r requirements.txt
   ```

3. **Hardware**: All components listed in [Hardware Requirements](#hardware-requirements)

### Clone the Repository

```bash
git clone https://github.com/engineer-ae3o/inverter_monitoring_system.git
cd inverter_monitoring_system
```

## Building the Project

### 1. Configure the Build

```bash
idf.py set-target esp32
idf.py menuconfig
```

In menuconfig, verify/adjust:
- Board configuration
- Flash settings
- Component configurations

### 2. Build the Project

```bash
idf.py build
```

### 3. Flash to Device

Connect your ESP32 via USB and run:

```bash
idf.py -p COM3 flash  # Replace COM3 with your serial port
```

On Linux/macOS:
```bash
idf.py -p /dev/ttyUSB0 flash
```

### 4. Monitor Serial Output

```bash
idf.py -p COM3 monitor
```

### 5. To build, flash and monior with one command
```bash
idf.py build flash monitor
```

## Usage

### Initial Boot

1. Power on the system
2. Bootup screen displays with Vhorde logo
3. System performs hardware initialization
4. Main UI appears showing current power metrics

### Navigation

- **NEXT Button (GPIO26)**: Navigate to the next information screen
- **PREV Button (GPIO25)**: Navigate to the previous information screen

### Display Screens

The system presents multiple screens with different information:
- Power metrics (voltage, current, power)
- Environmental data (temperature, humidity)
- System status (inverter state, battery state)
- Statistics and historical data

### Real-Time Monitoring

The display updates every 33ms with the latest measurements. Key metrics shown:

| Metric | Unit | Range |
|--------|------|-------|
| Voltage | V | 0-12.6V |
| Current | A | 0-25A |
| Power | VA | 0-300VA |
| Temperature | °C | -40 to +85 |
| Humidity | % | 0-100 |

## Configuration

### Task Priorities and Timing

Edit `components/config/config.hpp` to adjust:

```cpp
// Task stack sizes (in bytes)
constexpr uint16_t CALC_TASK_STACK_SIZE = 4096;
constexpr uint16_t DISPLAY_TASK_STACK_SIZE = 8192;
constexpr uint16_t AHT_TASK_STACK_SIZE = 3072;
constexpr uint16_t ADC_TASK_STACK_SIZE = 3072;
constexpr uint16_t LVGL_TASK_STACK_SIZE = 8192;

// Task priorities (0 = lowest, higher = more important)
constexpr uint16_t CALC_TASK_PRIORITY = 3;
constexpr uint16_t DISPLAY_TASK_PRIORITY = 5;
constexpr uint16_t AHT_TASK_PRIORITY = 4;
constexpr uint16_t ADC_TASK_PRIORITY = 5;
constexpr uint16_t LVGL_TASK_PRIORITY = 4;

// Measurement intervals
constexpr uint16_t AHT_READ_PERIOD_MS = 1600;    // Temperature/humidity
constexpr uint16_t ADC_READ_PERIOD_MS = 20;      // Power metrics
constexpr uint16_t LVGL_TASK_PERIOD_MS = 33;     // UI refresh (~30 FPS)
```

### GPIO Pins

Modify pin assignments in `config.hpp`:

```cpp
// I2C for AHT20
constexpr gpio_num_t AHT_SDA_PIN = GPIO_NUM_14;
constexpr gpio_num_t AHT_SCL_PIN = GPIO_NUM_27;

// ADC inputs
constexpr gpio_num_t CURRENT_SENSOR_PIN = GPIO_NUM_33;
constexpr gpio_num_t VOLTAGE_SENSOR_PIN = GPIO_NUM_32;

// SPI for LCD
constexpr gpio_num_t MOSI_PIN = GPIO_NUM_18;
constexpr gpio_num_t SCLK_PIN = GPIO_NUM_5;
constexpr gpio_num_t CS_PIN = GPIO_NUM_22;
constexpr gpio_num_t DC_PIN = GPIO_NUM_19;
constexpr gpio_num_t RST_PIN = GPIO_NUM_21;

// User input
constexpr gpio_num_t BUTTON_PREV_PIN = GPIO_NUM_25;
constexpr gpio_num_t BUTTON_NEXT_PIN = GPIO_NUM_26;
```

### Display Settings

```cpp
constexpr uint16_t LCD_WIDTH = 128;      // Horizontal resolution
constexpr uint16_t LCD_HEIGHT = 160;     // Vertical resolution
```

### Debug Mode

In `main/main.cpp`, toggle debug logging:

```cpp
#define DEBUG 1  // Set to 1 for debug output, 0 to disable
```

## Performance Metrics

### System Resource Usage

| Resource | Used | Total | Utilization |
|----------|------|-------|------------|
| DRAM | 144 KB | 177 KB | ~81.4% |
| IRAM | 94 KB | 128 KB | ~73% |
| Flash (Code) | 409 KB | ~3.2 MB | ~12.5% |

### Task Execution Times

| Task | Avg. Duration | Max. Duration |
|------|--------------|--------------|
| ADC Read | 5 ms | 10 ms |
| AHT Sensor | 150 ms | 200 ms |
| Calculation | 2 ms | 5 ms |
| Display Update | 10 ms | 20 ms |
| LVGL Render | 25 ms | 33 ms |

### Measurement Accuracy

| Parameter | Typical Accuracy | Calibration |
|-----------|-----------------|-------------|
| Voltage | ±2% | ADC calibration |
| Current | ±3% | Sensor-dependent |
| Temperature | ±0.5°C | AHT20 spec |
| Humidity | ±3% RH | AHT20 spec |

## Development

### Project Structure

```
inverter_monitoring_system/
├── .devcontainer/
├── .vscode/
├── components/
│   ├── power/                # Power monitoring
│   ├── aht/                  # Temperature/humidity sensor
│   ├── display/              # UI and graphics
│   ├── screens/              # UI screens
│   ├── alert/                # Alert subsystem
│   ├── utils/                # Graphics utilities
│   ├── button/               # Input handling and led control
│   ├── system/               # System utilities and calculations
│   ├── st7735/               # LCD driver
│   └── config/               # Configuration
├── main/
│   ├── main.cpp              # Application entry point
│   ├── CMakeLists.txt
│   ├── lv_conf.h             # LVGL configuration. Copy to your lvgl folder (pulled in automatically when you build)
│   └── idf_component.yml     # Library dependencies
├── .clangd
├── .gitignore
├── CMakeLists.txt
├── dependencies.lock
├── LICENSE
├── README.md                 # Project description - You're here
└── sdkconfig                 # Project configuration
```

### Adding New Components

1. Create a new directory under `components/`:
   ```bash
   mkdir components/my_component
   ```

2. Create `CMakeLists.txt`:
   ```cmake
   idf_component_register(SRCS "my_component.cpp"
                          INCLUDE_DIRS "."
                          REQUIRES freertos esp_common)
   ```

3. Add header and source files as needed

4. Include in main `CMakeLists.txt` if not auto detected by CMake

### Building for Development

Clean build:
```bash
idf.py fullclean
idf.py build
```

Incremental build:
```bash
idf.py build
```

### Code Style

- **C++ Standard**: C++17
- **Naming**: snake_case for variables/functions
- **Namespaces**: Use namespaces to organize components
- **Documentation**: Doxygen-style comments for public APIs

## Troubleshooting

### Common Issues

#### 1. **Flash Fails with "Permission Denied"**
- **Linux/macOS**: Add user to dialout group
  ```bash
  sudo usermod -a -G dialout $USER
  newgrp dialout
  ```
- **Windows**: Run as Administrator or update CH340 drivers

#### 2. **Display Shows No Output**
- Verify SPI pin connections
- Check `config.hpp` pin assignments match hardware
- Ensure LCD has proper power supply (~3.3V)
- Test with `idf.py monitor` to see debug messages

#### 3. **ADC Readings Are Unstable**
- Add decoupling capacitors (0.1μF) near ADC inputs
- Verify sensor output is 0-3.3V range
- Check ADC calibration settings
- Review analog circuit layout for noise

#### 4. **AHT20 Not Responding**
- Verify I2C clock and data lines have pull-up resistors (4.7k typical)
- Check I2C bus frequency (100 kHz recommended)
- Confirm GPIO14 (SDA) and GPIO27 (SCL) are correct
- Monitor with `idf.py monitor` for I2C errors

#### 5. **Buttons Not Responding**
- Verify debounce delay isn't too aggressive
- Check GPIO input levels with multimeter
- Ensure pull-up resistors are correct
- Review button event handling in `components/button/button_handler.cpp`

#### 6. **Task Watchdog False Triggers**
- Increase watchdog timeout in menuconfig
- Reduce task workload or increase stack size
- Check for infinite loops or blocking operations
- Set `DEBUG` to 1 to identify problematic task

#### 7. **Memory Corruption Errors**
- Increase stack sizes in `config.hpp`
- Check for buffer overflows in sensor data handling
- Verify queue sizes are sufficient
- Monitor free heap space with `ESP_LOGI("mem", "Free heap: %d", esp_get_free_heap_size())`

### Debugging

Enable debug logging in `main/main.cpp`:

```cpp
#define DEBUG 1
```
``` bash
idf.py monitor  # View logs
```

Use breakpoints with JTAG debugger (if your ESP32 module has one inbuilt):
```bash
idf.py openocd
# Open another terminal:
xtensa-esp32-elf-gdb build/inverter_monitoring_system.elf
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

**Copyright © 2025 engineer-ae3o**

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

### Guidelines
1. Fork the repository
2. Create a feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m Add "AmazingFeatur added"`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

### Code Review Process
- Ensure all changes follow the project's code style
- Add/update documentation as needed
- Test changes on hardware
- Update README.md if adding new features

---

**For questions or issues, please open an issue on the GitHub repository.**

Last Updated: December 12, 2025
