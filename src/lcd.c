#include "lcd.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_rom_sys.h"

#include "hardware_config.h"

static const uint8_t row_addresses[LCD_ROWS] = {0x80, 0xC0};

static void write_nibble(uint8_t nibble)
{
    gpio_set_level(PIN_DISPLAY_E, 0);
    gpio_set_level(PIN_DISPLAY_D4, (nibble >> 0) & 1);
    gpio_set_level(PIN_DISPLAY_D5, (nibble >> 1) & 1);
    gpio_set_level(PIN_DISPLAY_D6, (nibble >> 2) & 1);
    gpio_set_level(PIN_DISPLAY_D7, (nibble >> 3) & 1);
    esp_rom_delay_us(1);

    gpio_set_level(PIN_DISPLAY_E, 1);
    esp_rom_delay_us(2);
    gpio_set_level(PIN_DISPLAY_E, 0);
    esp_rom_delay_us(100);
}

static void write_byte(uint8_t value, bool data)
{
    gpio_set_level(PIN_DISPLAY_RS, data);
    esp_rom_delay_us(1);
    write_nibble(value >> 4);
    write_nibble(value & 0x0F);
}

static void write_command(uint8_t command)
{
    write_byte(command, false);
    if (command == 0x01 || command == 0x02) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void lcd_initialize(void)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_DISPLAY_RS, 0);
    gpio_set_level(PIN_DISPLAY_E, 0);

    write_nibble(0x03);
    vTaskDelay(pdMS_TO_TICKS(5));
    write_nibble(0x03);
    esp_rom_delay_us(150);
    write_nibble(0x03);
    esp_rom_delay_us(150);
    write_nibble(0x02);
    esp_rom_delay_us(150);

    write_command(0x28);
    write_command(0x0C);
    write_command(0x06);
    write_command(0x01);
}

static void clear(void)
{
    write_command(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));
}

static void set_cursor(uint8_t row, uint8_t column)
{
    if (row < LCD_ROWS && column < LCD_COLUMNS) {
        write_command(row_addresses[row] + column);
    }
}

void lcd_write_line(uint8_t row, const char *text)
{
    set_cursor(row, 0);
    for (int position = 0; position < LCD_COLUMNS; position++) {
        char character = ' ';
        if (text != NULL && *text != '\0') {
            character = *text++;
        }
        write_byte((uint8_t)character, true);
    }
}

void lcd_printf_line(uint8_t row, const char *format, ...)
{
    char text[LCD_COLUMNS + 1];
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    lcd_write_line(row, text);
}

void lcd_show(const char *top_line, const char *bottom_line)
{
    clear();
    lcd_write_line(0, top_line);
    lcd_write_line(1, bottom_line);
}
