#pragma once

#include <stdint.h>

void lcd_initialize(void);
void lcd_show(const char *top_line, const char *bottom_line);
void lcd_write_line(uint8_t row, const char *text);
void lcd_printf_line(uint8_t row, const char *format, ...);
