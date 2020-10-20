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

#pragma once

#include <stdint.h>

namespace eez {
namespace debug {

enum TraceType {
    TRACE_TYPE_INFO,
    TRACE_TYPE_DEBUG
};

void Trace(TraceType traceType, const char *format, ...);

} // namespace debug
} // namespace eez

#define InfoTrace(...) ::eez::debug::Trace(::eez::debug::TRACE_TYPE_INFO, __VA_ARGS__)

#ifdef DEBUG

#define DebugTrace(...) ::eez::debug::Trace(::eez::debug::TRACE_TYPE_DEBUG, __VA_ARGS__)

#else // NO DEBUG

#define DebugTrace(...) 0

#endif

#ifdef DEBUG

namespace eez {
namespace debug {

class DebugVariable {
public:
    DebugVariable(const char *name, uint32_t refreshRateMs);

    const char *name();

    virtual void tick1secPeriod() = 0;
    virtual void tick10secPeriod() = 0;
    virtual void dump(char *buffer) = 0;

    uint32_t getRefreshRateMs() { return m_refreshRateMs; }

private:
    const char *m_name;
    uint32_t m_refreshRateMs;
};

class DebugValueVariable : public DebugVariable {
public:
    DebugValueVariable(const char *name, uint32_t refreshRateMs = 1000);

    int32_t get() {
        return m_value;
    }
    void set(int32_t value) {
        m_value = value;
    }

    void tick1secPeriod();
    void tick10secPeriod();
    void dump(char *buffer);

private:
    int32_t m_value;
};

class DebugDurationForPeriod {
public:
    DebugDurationForPeriod();

    void tick(uint32_t duration);
    void tickPeriod();

    void dump(char *buffer);

private:
    uint32_t m_min;
    uint32_t m_max;
    uint32_t m_total;
    uint32_t m_count;

    uint32_t m_minLast;
    uint32_t m_maxLast;
    uint32_t m_avgLast;
};

class DebugDurationVariable : public DebugVariable {
public:
    DebugDurationVariable(const char *name, uint32_t refreshRateMs = 1000);

    void start();
    void finish();
    void tick(uint32_t tickCount);

    void tick1secPeriod();
    void tick10secPeriod();
    void dump(char *buffer);

private:
    uint32_t m_lastTickCount;

    DebugDurationForPeriod duration1sec;
    DebugDurationForPeriod duration10sec;

    uint32_t m_minTotal;
    uint32_t m_maxTotal;
};

class DebugCounterForPeriod {
public:
    DebugCounterForPeriod();

    void inc();
    void tickPeriod();
    void dump(char *buffer);

private:
    uint32_t m_counter;
    uint32_t m_lastCounter;
};

class DebugCounterVariable : public DebugVariable {
public:
    DebugCounterVariable(const char *name, uint32_t refreshRateMs = 1000);

    void inc();

    void tick1secPeriod();
    void tick10secPeriod();
    void dump(char *buffer);

private:
    DebugCounterForPeriod counter1sec;
    DebugCounterForPeriod counter10sec;

    uint32_t m_totalCounter;
};

} // namespace debug
} // namespace eez

#endif
