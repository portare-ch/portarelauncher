#ifndef PL_PS_SERIAL_H
#define PL_PS_SERIAL_H
#include <stddef.h>
#define PS_SERIAL_SIZE 11
int ps_serial_valid(const char *s);
int ps_boot_serial(const char *config, size_t size, char serial[PS_SERIAL_SIZE]);
#endif
