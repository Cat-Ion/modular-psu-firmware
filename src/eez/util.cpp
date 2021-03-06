/*
* EEZ Generic Firmware
* Copyright (C) 2018-present, Envox d.o.o.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.

* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.

* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <eez/util.h>

#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#if defined(EEZ_PLATFORM_STM32)
#include <crc.h>
#endif

namespace eez {

float remap(float x, float x1, float y1, float x2, float y2) {
    return y1 + (x - x1) * (y2 - y1) / (x2 - x1);
}

float remapQuad(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t * t;
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}

float remapOutQuad(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t * (2 - t);
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}

float remapInOutQuad(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t < .5 ? 2 * t*t : -1 + (4 - 2 * t)*t;
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}

float remapCubic(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t * t * t;
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}

float remapOutCubic(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t - 1;
    t = 1 + t * t * t;
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}

float remapExp(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t == 0 ? 0 : float(pow(2, 10 * (t - 1)));
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}

float remapOutExp(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t == 1 ? 1 : float(1 - pow(2, -10 * t));
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}

float clamp(float x, float min, float max) {
    if (x < min) {
        return min;
    }
    if (x > max) {
        return max;
    }
    return x;
}

void strcatInt(char *str, int value) {
    str = str + strlen(str);
    sprintf(str, "%d", value);
}

void strcatInt32(char *str, int32_t value) {
    str = str + strlen(str);
    sprintf(str, "%ld", (long)value);
}

void strcatUInt32(char *str, uint32_t value) {
    str = str + strlen(str);
    sprintf(str, "%lu", (unsigned long)value);
}

void strcatFloat(char *str, float value) {
    sprintf(str + strlen(str), "%g", value);
}

void strcatFloat(char *str, float value, int numDecimalPlaces) {
    sprintf(str + strlen(str), "%.*f", numDecimalPlaces, value);
}

void strcatVoltage(char *str, float value) {
    strcatFloat(str, value);
    strcat(str, "V");
}

void strcatCurrent(char *str, float value) {
    strcatFloat(str, value);
    strcat(str, "A");
}

void strcatPower(char *str, float value) {
    strcatFloat(str, value);
    strcat(str, "W");
}

void strcatDuration(char *str, float value) {
    if (value > 0.1) {
        strcatFloat(str, value);
        strcat(str, " s");
    } else {
        strcatFloat(str, value * 1000);
        strcat(str, " ms");
    }
}

void strcatLoad(char *str, float value) {
    if (value < 1000) {
        strcatFloat(str, value);
        strcat(str, " ohm");
    } else if (value < 1000000) {
        strcatFloat(str, value / 1000);
        strcat(str, " Kohm");
    } else {
        strcatFloat(str, value / 1000000);
        strcat(str, " Mohm");
    }
}

#if defined(EEZ_PLATFORM_STM32)
uint32_t crc32(const uint8_t *mem_block, size_t block_size) {
	return HAL_CRC_Calculate(&hcrc, (uint32_t *)mem_block, block_size);
}
#else
/*
From http://www.hackersdelight.org/hdcodetxt/crc.c.txt:

This is the basic CRC-32 calculation with some optimization but no
table lookup. The the byte reversal is avoided by shifting the crc reg
right instead of left and by using a reversed 32-bit word to represent
the polynomial.
When compiled to Cyclops with GCC, this function executes in 8 + 72n
instructions, where n is the number of bytes in the input message. It
should be doable in 4 + 61n instructions.
If the inner loop is strung out (approx. 5*8 = 40 instructions),
it would take about 6 + 46n instructions.
*/

uint32_t crc32(const uint8_t *mem_block, size_t block_size) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < block_size; ++i) {
        uint32_t byte = mem_block[i]; // Get next byte.
        crc = crc ^ byte;
        for (int j = 0; j < 8; ++j) { // Do eight times.
            uint32_t mask = -((int32_t)crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return ~crc;
}
#endif

uint8_t toBCD(uint8_t bin) {
    return ((bin / 10) << 4) | (bin % 10);
}

uint8_t fromBCD(uint8_t bcd) {
    return ((bcd >> 4) & 0xF) * 10 + (bcd & 0xF);
}

float roundPrec(float a, float prec) {
    float r = 1 / prec;
    return roundf(a * r) / r;
}

float floorPrec(float a, float prec) {
    float r = 1 / prec;
    return floorf(a * r) / r;
}

float ceilPrec(float a, float prec) {
    float r = 1 / prec;
    return ceilf(a * r) / r;
}

bool isNaN(float x) {
    return x != x;
}

bool isDigit(char ch) {
    return ch >= '0' && ch <= '9';
}

bool isHexDigit(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

bool isUperCaseLetter(char ch) {
    return ch >= 'A' && ch <= 'Z';
}

char toHexDigit(int num) {
    if (num >= 0 && num <= 9) {
        return '0' + num;
    } else {
        return 'A' + (num - 10);
    }
}

int fromHexDigit(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }

    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }

    return 10 + (ch - 'A');
}

bool pointInsideRect(int xPoint, int yPoint, int xRect, int yRect, int wRect, int hRect) {
    return xPoint >= xRect && xPoint < xRect + wRect && yPoint >= yRect && yPoint < yRect + hRect;
}

void getParentDir(const char *path, char *parentDirPath) {
    int lastPathSeparatorIndex;

    for (lastPathSeparatorIndex = strlen(path) - 1;
         lastPathSeparatorIndex >= 0 && path[lastPathSeparatorIndex] != PATH_SEPARATOR[0];
         --lastPathSeparatorIndex)
        ;

    int i;
    for (i = 0; i < lastPathSeparatorIndex; ++i) {
        parentDirPath[i] = path[i];
    }
    parentDirPath[i] = 0;
}

bool parseMacAddress(const char *macAddressStr, size_t macAddressStrLength, uint8_t *macAddress) {
    int state = 0;
    int a;
    int i = 0;
    uint8_t resultMacAddress[6];

    const char *end = macAddressStr + macAddressStrLength;
    for (const char *p = macAddressStr; p < end; ++p) {
        if (state == 0) {
            if (*p == '-' || *p == ' ') {
                continue;
            } else if (isHexDigit(*p)) {
                a = fromHexDigit(*p);
                state = 1;
            } else {
                return false;
            }
        } else if (state == 1) {
            if (isHexDigit(*p)) {
                if (i < 6) {
                    resultMacAddress[i++] = (a << 4) | fromHexDigit(*p);
                    state = 0;
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }
    }

    if (state != 0 || i != 6) {
        return false;
    }

    memcpy(macAddress, resultMacAddress, 6);

    return true;
}

bool parseIpAddress(const char *ipAddressStr, size_t ipAddressStrLength, uint32_t &ipAddress) {
    const char *p = ipAddressStr;
    const char *q = ipAddressStr + ipAddressStrLength;

    uint8_t ipAddressArray[4];

    for (int i = 0; i < 4; ++i) {
        if (p == q) {
            return false;
        }

        uint32_t part = 0;
        for (int j = 0; j < 3; ++j) {
            if (p == q) {
                if (j > 0 && i == 3) {
                    break;
                } else {
                    return false;
                }
            } else if (isDigit(*p)) {
                part = part * 10 + (*p++ - '0');
            } else if (j > 0 && *p == '.') {
                break;
            } else {
                return false;
            }
        }

        if (part > 255) {
            return false;
        }

        if ((i < 3 && *p++ != '.') || (i == 3 && p != q)) {
            return false;
        }

        ipAddressArray[i] = part;
    }

    ipAddress = arrayToIpAddress(ipAddressArray);

    return true;
}

int getIpAddressPartA(uint32_t ipAddress) {
    return ((uint8_t *)&ipAddress)[0];
}

void setIpAddressPartA(uint32_t *ipAddress, uint8_t value) {
    ((uint8_t *)ipAddress)[0] = value;
}

int getIpAddressPartB(uint32_t ipAddress) {
    return ((uint8_t *)&ipAddress)[1];
}

void setIpAddressPartB(uint32_t *ipAddress, uint8_t value) {
    ((uint8_t *)ipAddress)[1] = value;
}

int getIpAddressPartC(uint32_t ipAddress) {
    return ((uint8_t *)&ipAddress)[2];
}

void setIpAddressPartC(uint32_t *ipAddress, uint8_t value) {
    ((uint8_t *)ipAddress)[2] = value;
}

int getIpAddressPartD(uint32_t ipAddress) {
    return ((uint8_t *)&ipAddress)[3];
}

void setIpAddressPartD(uint32_t *ipAddress, uint8_t value) {
    ((uint8_t *)ipAddress)[3] = value;
}

void ipAddressToArray(uint32_t ipAddress, uint8_t *ipAddressArray) {
    ipAddressArray[0] = getIpAddressPartA(ipAddress);
    ipAddressArray[1] = getIpAddressPartB(ipAddress);
    ipAddressArray[2] = getIpAddressPartC(ipAddress);
    ipAddressArray[3] = getIpAddressPartD(ipAddress);
}

uint32_t arrayToIpAddress(uint8_t *ipAddressArray) {
    return getIpAddress(ipAddressArray[0], ipAddressArray[1], ipAddressArray[2], ipAddressArray[3]);
}

uint32_t getIpAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    uint32_t ipAddress;

    setIpAddressPartA(&ipAddress, a);
    setIpAddressPartB(&ipAddress, b);
    setIpAddressPartC(&ipAddress, c);
    setIpAddressPartD(&ipAddress, d);

    return ipAddress;
}

void ipAddressToString(uint32_t ipAddress, char *ipAddressStr) {
    sprintf(ipAddressStr, "%d.%d.%d.%d", getIpAddressPartA(ipAddress), getIpAddressPartB(ipAddress),
            getIpAddressPartC(ipAddress), getIpAddressPartD(ipAddress));
}

void macAddressToString(const uint8_t *macAddress, char *macAddressStr) {
    for (int i = 0; i < 6; ++i) {
        macAddressStr[3 * i] = toHexDigit((macAddress[i] & 0xF0) >> 4);
        macAddressStr[3 * i + 1] = toHexDigit(macAddress[i] & 0xF);
        macAddressStr[3 * i + 2] = i < 5 ? '-' : 0;
    }
}

void formatTimeZone(int16_t timeZone, char *text, int count) {
    if (timeZone == 0) {
        strncpy(text, "GMT", count - 1);
    } else {
        char sign;
        int16_t value;
        if (timeZone > 0) {
            sign = '+';
            value = timeZone;
        } else {
            sign = '-';
            value = -timeZone;
        }
        snprintf(text, count - 1, "%c%02d:%02d GMT", sign, value / 100, value % 100);
    }
    text[count - 1] = 0;
}

bool parseTimeZone(const char *timeZoneStr, size_t timeZoneLength, int16_t &timeZone) {
    int state = 0;

    int sign = 1;
    int integerPart = 0;
    int fractionPart = 0;

    const char *end = timeZoneStr + timeZoneLength;
    for (const char *p = timeZoneStr; p < end; ++p) {
        if (*p == ' ') {
            continue;
        }

        if (state == 0) {
            if (*p == '+') {
                state = 1;
            } else if (*p == '-') {
                sign = -1;
                state = 1;
            } else if (isDigit(*p)) {
                integerPart = *p - '0';
                state = 2;
            } else {
                return false;
            }
        } else if (state == 1) {
            if (isDigit(*p)) {
                integerPart = (*p - '0');
                state = 2;
            } else {
                return false;
            }
        } else if (state == 2) {
            if (*p == ':') {
                state = 4;
            } else if (isDigit(*p)) {
                integerPart = integerPart * 10 + (*p - '0');
                state = 3;
            } else {
                return false;
            }
        } else if (state == 3) {
            if (*p == ':') {
                state = 4;
            } else {
                return false;
            }
        } else if (state == 4) {
            if (isDigit(*p)) {
                fractionPart = (*p - '0');
                state = 5;
            } else {
                return false;
            }
        } else if (state == 5) {
            if (isDigit(*p)) {
                fractionPart = fractionPart * 10 + (*p - '0');
                state = 6;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    if (state != 2 && state != 3 && state != 6) {
        return false;
    }

    int value = sign * (integerPart * 100 + fractionPart);

    if (value < -1200 || value > 1400) {
        return false;
    }

    timeZone = (int16_t)value;

    return true;
}

void replaceCharacter(char *str, char ch, char repl) {
    while (*str) {
        if (*str == ch) {
            *str = repl;
        }
        ++str;
    }
}

int strcicmp(char const *a, char const *b) {
    for (;; a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d != 0 || !*a)
            return d;
    }
}

int strncicmp(char const *a, char const *b, int n) {
    for (; n--; a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d != 0 || !*a)
            return d;
    }
    return 0;
}

bool isStringEmpty(char const *s) {
    for (; *s; s++) {
        if (!isspace(*s)) {
            return false;
        }
    }
    return true;
}

bool startsWith(const char *str, const char *prefix) {
    if (!str || !prefix)
        return false;
    size_t strLen = strlen(str);
    size_t prefixLen = strlen(prefix);
    if (prefixLen > strLen)
        return false;
    return strncmp(str, prefix, prefixLen) == 0;
}

bool endsWith(const char *str, const char *suffix) {
    if (!str || !suffix)
        return false;
    size_t strLen = strlen(str);
    size_t suffixLen = strlen(suffix);
    if (suffixLen > strLen)
        return false;
    return strncmp(str + strLen - suffixLen, suffix, suffixLen) == 0;
}

bool endsWithNoCase(const char *str, const char *suffix) {
    if (!str || !suffix)
        return false;
    size_t strLen = strlen(str);
    size_t suffixLen = strlen(suffix);
    if (suffixLen > strLen)
        return false;
    return strncicmp(str + strLen - suffixLen, suffix, suffixLen) == 0;
}

void formatBytes(uint64_t bytes, char *text, int count) {
    if (bytes == 0) {
        strncpy(text, "0 Bytes", count - 1);
    } else {
        double c = 1024.0;
        const char *e[] = { "Bytes", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };
        uint64_t f = (uint64_t)floor(log((double)bytes) / log(c));
        double g = round((bytes / pow(c, (double)f)) * 100) / 100;
        snprintf(text, count - 1, "%g %s", g, e[f]);
    }
    text[count - 1] = 0;
}

void getBaseFileName(const char *path, char *baseName, unsigned basenameSize) {
    const char *a = strrchr(path, '/');
    if (a) {
         a++;
    } else {
        a = path;
    }
    
    const char *b = strrchr(path, '.');
    if (!b || !(b >= a)) {
        b = path + strlen(path);
    }

    unsigned n = b - a;
    n = MIN(basenameSize - 1, n);
    if (n > 0) {
        strncpy(baseName, a, n);
    }
    baseName[n] = 0;
}

} // namespace eez
