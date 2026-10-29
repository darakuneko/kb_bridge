// Minimal Arduino.h stand-in for host-side tests of the nRF sketch's pure logic.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>

uint32_t millis();                          // provided by the test
